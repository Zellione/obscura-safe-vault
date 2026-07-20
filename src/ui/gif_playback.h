#pragma once

#include <SDL3/SDL.h>

#include <memory>

// Self-contained animated GIF playback component (Phase 47). Owns the decoder,
// the RGBA texture, and the frame-advance clock; hosted by ImageViewer when
// the current leaf item is an animated GIF. All frame-advance logic lives in
// the pure ui::gif_frames_to_advance; this is the SDL + FFmpeg plumbing on top.
//
// pImpl: the FFmpeg-bearing implementation is gated by OSV_VENDORED_AV *inside*
// gif_playback.cpp, so this header (and therefore ImageViewer) compiles
// everywhere. On a build without vendored FFmpeg, valid() returns false and the
// host falls back to showing the static first frame.
//
// Lifetime: borrows the unlocked vault's file handle + master key through the
// decrypted image bytes. The host destroys this before any vault lock / idle /
// switch.
namespace gfx { class Renderer; }
namespace vault { class Vault; struct IndexNode; }

namespace ui {

class GifPlayback {
public:
    GifPlayback(const vault::Vault& vault, const vault::IndexNode& node);
    ~GifPlayback();

    GifPlayback(const GifPlayback&)            = delete;
    GifPlayback& operator=(const GifPlayback&) = delete;

    // True when the decoder opened a decodable animated GIF. False on an
    // undecodable file, a static GIF, or a non-FFmpeg build.
    [[nodiscard]] bool valid() const noexcept;

    // True while playing — the host reports animating() so the event loop keeps
    // ticking. False when paused / invalid.
    [[nodiscard]] bool animating() const noexcept;

    // True if playback is paused (Space toggle).
    [[nodiscard]] bool paused() const noexcept;

    // Number of frames shown (for testing/debug).
    [[nodiscard]] size_t frames_shown() const noexcept;

    // Space toggles pause/play.
    void toggle_pause() noexcept;

    void update(double dt);   // advance the frame clock
    void render(gfx::Renderer& r, const SDL_FRect& dest);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ui
