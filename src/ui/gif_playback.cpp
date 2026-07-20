#include "ui/gif_playback.h"

#include <SDL3/SDL.h>

#ifdef OSV_VENDORED_AV

#include <cstring>
#include <memory>
#include <optional>
#include <print>

#include "crypto/secure_mem.h"
#include "gfx/renderer.h"
#include "media/gif_decoder.h"
#include "ui/gif_model.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace ui {

// Full implementation: a SecureBytes holds the decrypted GIF; a GifDecoder
// reads frames from it one at a time; decoded RGBA frames upload to a
// streaming texture. The pure gif_frames_to_advance owns the playback clock.
// No bytes touch disk (invariant #1).
struct GifPlayback::Impl {
    crypto::SecureBytes     bytes_;             // decrypted GIF data (borrowed by decoder_)
    media::GifDecoder       dec_;               // reads frames from bytes_
    media::GifFrame         current_;           // frame on screen
    SDL_Texture*            tex_ = nullptr;     // RGBA streaming texture (created lazily)
    double                  acc_ = 0.0;         // frame-advance accumulator
    bool                    paused_ = false;    // Space toggle
    size_t                  shown_ = 0;         // frames shown (for testing/debug)
    bool                    valid_ = false;     // decoder opened successfully
    bool                    dirty_ = false;     // texture needs re-upload

    Impl(const vault::Vault& vault, const vault::IndexNode& node)
    {
        // Read the decrypted GIF into mlock'd SecureBytes
        if (vault.read_image(node, bytes_) != vault::VaultResult::Ok) {
            std::println(stderr, "[GifPlayback] read_image failed");
            return;
        }

        // Open the decoder, which borrows bytes_ (must stay alive)
        if (!dec_.open(bytes_.as_span())) {
            std::println(stderr, "[GifPlayback] decoder open failed");
            return;
        }

        // Decode the first frame
        auto f = dec_.next_frame();
        if (!f) {
            std::println(stderr, "[GifPlayback] failed to decode first frame");
            return;
        }
        current_ = std::move(*f);

        // Check if there's a second frame to confirm it's animated (not static).
        // If there is, rewind so the decoder is back at the start.
        if (auto f2 = dec_.next_frame(); !f2) {
            // Only one frame: static GIF, reject for animation playback
            std::println(stderr, "[GifPlayback] single-frame GIF, not animated");
            return;
        }
        // GIF has at least 2 frames: it's animated. Rewind to start.
        dec_.rewind();
        shown_ = 1;  // First frame is on display from construction

        valid_ = true;
        dirty_ = true;  // upload the first frame on first render
    }

    ~Impl()
    {
        if (tex_ != nullptr) {
            SDL_DestroyTexture(tex_);
        }
        // bytes_ is wiped by SecureBytes destructor
        // dec_ is destroyed, releasing its reference to bytes_
    }

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] bool animating() const { return valid_ && !paused_; }
    [[nodiscard]] bool paused() const { return paused_; }
    [[nodiscard]] size_t frames_shown() const { return shown_; }
    [[nodiscard]] size_t frame_count() const { return dec_.frames_decoded(); }

    void toggle_pause() { paused_ = !paused_; }

    void update(double dt)
    {
        if (!valid_) {
            return;
        }

        const int steps = gif_frames_to_advance(acc_, dt, current_.delay_s, paused_);
        for (int i = 0; i < steps; ++i) {
            auto f = dec_.next_frame();
            if (!f) {
                // End of stream: rewind and loop
                dec_.rewind();
                f = dec_.next_frame();
                if (!f) {
                    // Undecodable on rewind: hold the last frame and pause
                    paused_ = true;
                    return;
                }
            }
            current_ = std::move(*f);
            ++shown_;
            dirty_ = true;
        }
    }

    void render(gfx::Renderer& r, const SDL_FRect& dest)
    {
        if (!valid_) {
            return;
        }

        SDL_Renderer* sdl_r = r.sdl();
        if (sdl_r == nullptr) {
            return;
        }

        // Lazy texture creation on first render
        if (tex_ == nullptr) {
            tex_ = SDL_CreateTexture(sdl_r, SDL_PIXELFORMAT_RGBA8888,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    current_.width, current_.height);
            if (tex_ == nullptr) {
                std::println(stderr, "[GifPlayback] texture creation failed: {}",
                             SDL_GetError());
                return;
            }
        }

        // Upload the current frame to the texture if it changed
        if (dirty_) {
            upload_current_frame();
            dirty_ = false;
        }

        // Draw the texture at the destination rect
        r.draw_image(tex_, dest);
    }

    // Copy current_.rgba into the streaming texture, honoring SDL's row pitch.
    // Split out of render() to keep nesting shallow (SonarQube S134).
    void upload_current_frame()
    {
        int pitch = 0;
        void* pixels = nullptr;
        if (!SDL_LockTexture(tex_, nullptr, &pixels, &pitch)) {
            return;
        }
        const size_t row_bytes = static_cast<size_t>(current_.width) * 4;
        if (const size_t byte_size = row_bytes * static_cast<size_t>(current_.height);
            current_.rgba.size() == byte_size && pixels != nullptr && pitch > 0) {
            const auto pitch_size = static_cast<size_t>(pitch);
            // The pitch (byte stride per row) may exceed width*4 due to driver
            // alignment, so copy row-by-row rather than as one flat block.
            for (size_t y = 0; y < static_cast<size_t>(current_.height); ++y) {
                const uint8_t* src = current_.rgba.data() + (y * row_bytes);
                uint8_t* dst = static_cast<uint8_t*>(pixels) + (y * pitch_size);
                std::memcpy(dst, src, row_bytes);
            }
        }
        SDL_UnlockTexture(tex_);
    }
};

#else  // !OSV_VENDORED_AV — playback unavailable; host shows the static first frame.

struct GifPlayback::Impl {
    Impl(const vault::Vault&, const vault::IndexNode&) {}
    [[nodiscard]] bool valid() const { return false; }
    [[nodiscard]] bool animating() const { return false; }
    [[nodiscard]] bool paused() const { return false; }
    [[nodiscard]] size_t frames_shown() const { return 0; }
    void toggle_pause() {}
    void update(double) {}
    void render(gfx::Renderer&, const SDL_FRect&) {}
};

#endif  // OSV_VENDORED_AV

// --- Public API -----

GifPlayback::GifPlayback(const vault::Vault& vault, const vault::IndexNode& node)
    : impl_(std::make_unique<Impl>(vault, node))
{
}

GifPlayback::~GifPlayback() = default;

bool GifPlayback::valid() const noexcept { return impl_->valid(); }
bool GifPlayback::animating() const noexcept { return impl_->animating(); }
bool GifPlayback::paused() const noexcept { return impl_->paused(); }
size_t GifPlayback::frames_shown() const noexcept { return impl_->frames_shown(); }
size_t GifPlayback::frame_count() const noexcept { return impl_->frame_count(); }
void GifPlayback::toggle_pause() noexcept { impl_->toggle_pause(); }
void GifPlayback::update(double dt) { impl_->update(dt); }
void GifPlayback::render(gfx::Renderer& r, const SDL_FRect& dest)
{
    impl_->render(r, dest);
}

}  // namespace ui
