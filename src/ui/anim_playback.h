#pragma once

#include <SDL3/SDL.h>

#include <memory>

// Self-contained animation playback component (Phase 47 for GIF, Phase 57 for
// WebP). Owns the decoder, the RGBA texture, and the frame-advance clock; hosted
// by ImageViewer and GalleryGrid when the item is animated. All frame-advance
// logic lives in the pure ui::anim_frames_to_advance; this is the SDL plumbing
// on top.
//
// The backend is chosen from the node's format: WebP decodes through libwebp
// (always available), GIF through FFmpeg (OSV_VENDORED_AV only). pImpl keeps
// both out of this header, so it compiles everywhere. When no backend exists for
// a format in this build, valid() returns false and the host falls back to
// showing the static first frame.
//
// Playback loops forever: a file's declared loop count is deliberately ignored,
// so GIF and WebP behave identically.
//
// Lifetime: borrows the unlocked vault's file handle + master key through the
// decrypted image bytes. The host destroys this before any vault lock / idle /
// switch.
namespace gfx { class Renderer; }
namespace vault { class Vault; struct IndexNode; }

namespace ui {

class AnimPlayback {
public:
    AnimPlayback(const vault::Vault& vault, const vault::IndexNode& node);
    ~AnimPlayback();

    AnimPlayback(const AnimPlayback&)            = delete;
    AnimPlayback& operator=(const AnimPlayback&) = delete;

    // True when a backend opened a decodable animation. False on an
    // undecodable file, a single-frame image, or a format with no backend in
    // this build.
    [[nodiscard]] bool valid() const noexcept;

    // True while playing — the host reports animating() so the event loop keeps
    // ticking. False when paused / invalid.
    [[nodiscard]] bool animating() const noexcept;

    // True if playback is paused (Space toggle).
    [[nodiscard]] bool paused() const noexcept;

    // Number of frames shown (for testing/debug).
    [[nodiscard]] size_t frames_shown() const noexcept;

    // Number of frames decoded so far. Used to enforce the hover budget:
    // the frame count is unknown until the animation has been partially or fully
    // decoded. This accessor allows GalleryGrid to monitor the count during
    // playback and stop if it exceeds kAnimHoverMaxFrames (300).
    [[nodiscard]] size_t frame_count() const noexcept;

    // Space toggles pause/play.
    void toggle_pause() noexcept;

    void update(double dt);   // advance the frame clock
    void render(gfx::Renderer& r, const SDL_FRect& dest);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ui
