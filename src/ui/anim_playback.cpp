#include "ui/anim_playback.h"

#include <SDL3/SDL.h>

#include <cstring>
#include <memory>
#include <optional>
#include <print>

#include "crypto/secure_mem.h"
#include "gfx/renderer.h"
#include "media/anim_decoder.h"
#include "media/webp_anim_decoder.h"
#include "ui/anim_model.h"
#include "ui/widgets.h"
#include "vault/index.h"
#include "vault/vault.h"

#ifdef OSV_VENDORED_AV
#include "media/gif_decoder.h"
#endif

namespace ui {

namespace {

// Picks the decoder for a node's format. GIF needs vendored FFmpeg; WebP does
// not, so an animated WebP plays in every build. Any other format returns
// nullptr, which surfaces as valid() == false and leaves the host showing the
// static first frame.
std::unique_ptr<media::AnimDecoder> make_decoder(vault::ImageFormat format)
{
    switch (format) {
    case vault::ImageFormat::WebP:
        return std::make_unique<media::WebpAnimDecoder>();
#ifdef OSV_VENDORED_AV
    case vault::ImageFormat::GIF:
        return std::make_unique<media::GifDecoder>();
#endif
    default:
        return nullptr;
    }
}

} // namespace

// A SecureBytes holds the decrypted image; an AnimDecoder backend reads frames
// from it one at a time; decoded RGBA frames upload to a streaming texture. The
// pure anim_frames_to_advance owns the playback clock. No bytes touch disk
// (invariant #1).
struct AnimPlayback::Impl {
    crypto::SecureBytes                 bytes_;   // decrypted image (borrowed by dec_)
    std::unique_ptr<media::AnimDecoder> dec_;     // reads frames from bytes_
    media::AnimFrame         current_;           // frame on screen
    SDL_Texture*            tex_ = nullptr;     // RGBA streaming texture (created lazily)
    double                  acc_ = 0.0;         // frame-advance accumulator
    bool                    paused_ = false;    // Space toggle
    size_t                  shown_ = 0;         // frames shown (for testing/debug)
    bool                    valid_ = false;     // decoder opened successfully
    bool                    dirty_ = false;     // texture needs re-upload

    Impl(const vault::Vault& vault, const vault::IndexNode& node)
    {
        // No backend for this format in this build (a GIF without vendored
        // FFmpeg, or a format that cannot animate at all).
        dec_ = make_decoder(node.meta.format);
        if (!dec_) {
            return;
        }

        // Read the decrypted image into mlock'd SecureBytes
        if (vault.read_image(node, bytes_) != vault::VaultResult::Ok) {
            std::println(stderr, "[AnimPlayback] read_image failed");
            return;
        }

        // Open the decoder, which borrows bytes_ (must stay alive)
        if (!dec_->open(bytes_.as_span())) {
            std::println(stderr, "[AnimPlayback] decoder open failed");
            return;
        }

        // Decode the first frame
        auto f = dec_->next_frame();
        if (!f) {
            std::println(stderr, "[AnimPlayback] failed to decode first frame");
            return;
        }
        current_ = std::move(*f);

        // A second frame confirms it really animates. WebpAnimDecoder::open
        // already rejects single-frame files, but GifDecoder does not, so the
        // check stays here for both. Rewind so playback starts from the top.
        if (auto f2 = dec_->next_frame(); !f2) {
            std::println(stderr, "[AnimPlayback] single-frame image, not animated");
            return;
        }
        dec_->rewind();
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
    [[nodiscard]] size_t frame_count() const { return dec_ ? dec_->frames_decoded() : 0; }

    void toggle_pause() { paused_ = !paused_; }

    void update(double dt)
    {
        if (!valid_) {
            return;
        }

        const int steps = anim_frames_to_advance(acc_, dt, current_.delay_s, paused_);
        for (int i = 0; i < steps; ++i) {
            auto f = dec_->next_frame();
            if (!f) {
                // End of stream: rewind and loop
                dec_->rewind();
                f = dec_->next_frame();
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

        // Lazy texture creation on first render. Both backends emit frames whose
        // bytes are R,G,B,A in memory order, which is SDL_PIXELFORMAT_RGBA32 —
        // an alias that resolves to ABGR8888 on a little-endian CPU. The packed
        // SDL_PIXELFORMAT_RGBA8888 is a *different* layout there (A,B,G,R in
        // memory): it renders every frame with red pinned to the source alpha
        // and green/blue transposed. Frames are opaque (the WebP backend
        // flattens alpha, GIF palettes come through opaque), so blending is off
        // — the draw then never depends on what alpha a decoder chose to emit.
        if (tex_ == nullptr) {
            tex_ = SDL_CreateTexture(sdl_r, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    current_.width, current_.height);
            if (tex_ == nullptr) {
                std::println(stderr, "[AnimPlayback] texture creation failed: {}",
                             SDL_GetError());
                return;
            }
            SDL_SetTextureBlendMode(tex_, SDL_BLENDMODE_NONE);
        }

        // Upload the current frame to the texture if it changed
        if (dirty_) {
            upload_current_frame();
            dirty_ = false;
        }

        // Aspect-fit rather than fill: the hover-preview call sites (gallery
        // tiles, list rows, the viewer's thumbnail strip) pass the whole square
        // cell, the same rect they pass ui::draw_tile_thumb and
        // gfx::draw_thumbnail_strip — both of which fit into it. Filling would
        // squash a non-square animation the instant the pointer touched its
        // tile. In the viewer paths `dest` already carries the image's aspect,
        // so the fit is a no-op and the backing below is skipped: no black seam
        // can creep in around a zoomed image.
        const SDL_FRect img =
            fit_rect(static_cast<float>(current_.width),
                     static_cast<float>(current_.height), dest);
        if (img.w < dest.w - 0.5f || img.h < dest.h - 0.5f) {
            r.draw_rect(dest, gfx::Color{0, 0, 0, 255});   // letterbox bands, as on static tiles
        }
        r.draw_image(tex_, img);
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

// --- Public API -----

AnimPlayback::AnimPlayback(const vault::Vault& vault, const vault::IndexNode& node)
    : impl_(std::make_unique<Impl>(vault, node))
{
}

AnimPlayback::~AnimPlayback() = default;

bool AnimPlayback::valid() const noexcept { return impl_->valid(); }
bool AnimPlayback::animating() const noexcept { return impl_->animating(); }
bool AnimPlayback::paused() const noexcept { return impl_->paused(); }
size_t AnimPlayback::frames_shown() const noexcept { return impl_->frames_shown(); }
size_t AnimPlayback::frame_count() const noexcept { return impl_->frame_count(); }
void AnimPlayback::toggle_pause() noexcept { impl_->toggle_pause(); }
void AnimPlayback::update(double dt) { impl_->update(dt); }
void AnimPlayback::render(gfx::Renderer& r, const SDL_FRect& dest)
{
    impl_->render(r, dest);
}

}  // namespace ui
