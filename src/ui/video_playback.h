#pragma once

#include <SDL3/SDL.h>

#include <memory>

// Self-contained video playback component (Phase 15 PR5). Owns the decoder,
// the streaming YUV texture and the transport clock; hosted by ImageViewer when
// the current leaf item is a video. All transport maths lives in the pure
// ui::PlaybackModel; this is the SDL + FFmpeg plumbing on top.
//
// pImpl: the FFmpeg-bearing implementation is gated by OSV_VENDORED_AV *inside*
// video_playback.cpp, so this header (and therefore ImageViewer) compiles
// everywhere. On a build without vendored FFmpeg, valid() returns false and the
// host falls back to showing the poster.
//
// Lifetime: borrows the unlocked vault's file handle + master key through the
// VideoSource. The host destroys this before any vault lock / idle / switch.
namespace gfx { class Renderer; class FontAtlas; }
namespace vault { class Vault; struct IndexNode; }

namespace ui {

class VideoPlayback {
public:
    VideoPlayback(const vault::Vault& vault, const vault::IndexNode& node);
    ~VideoPlayback();

    VideoPlayback(const VideoPlayback&)            = delete;
    VideoPlayback& operator=(const VideoPlayback&) = delete;

    // True when the decoder opened a supported video stream. False on an
    // unsupported codec, a decode-open failure, or a non-FFmpeg build.
    [[nodiscard]] bool valid() const noexcept;

    // True while playing — the host reports animating() so the event loop keeps
    // ticking. False when paused / at end / invalid.
    [[nodiscard]] bool animating() const noexcept;

    // True if the video stream contains an audio track (Phase 16).
    [[nodiscard]] bool has_audio() const noexcept;

    // Current playback position in seconds (Phase 16, for testing/debug).
    [[nodiscard]] double position() const noexcept;

    void update(double dt);   // advance the clock (decode happens lazily in render)
    void render(gfx::Renderer& r, gfx::FontAtlas& font, const SDL_FRect& area);

    void handle_key(SDL_Keycode key);                       // Space / , / . / J / L
    void handle_mouse_down(float mx, float my);             // seek-bar scrub start + jump
    void handle_mouse_motion(float mx, float my, bool left_down);
    void handle_mouse_up();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ui
