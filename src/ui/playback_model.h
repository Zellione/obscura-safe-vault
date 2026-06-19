#pragma once

#include <string>

// Pure transport maths for the video player (Phase 15). Like slideshow_model /
// scroll_model it is deliberately SDL-, GPU- and FFmpeg-free so the clock,
// clamping, seek-bar mapping and time formatting can be unit-tested headlessly;
// ui::VideoPlayback owns the decoder + textures and drives this from update(dt).
//
// Video-only clock (no audio in Phase 15): tick(dt) advances the position while
// playing(), clamping to [0, duration] and auto-pausing at the end. The displayed
// frame is chosen by frame_due(clock, frame_pts).
namespace ui {

inline constexpr double PLAYBACK_SEEK_STEP = 5.0;   // J / L seek delta (seconds)

// Clamp a time to [0, duration] (duration <= 0 collapses to 0).
[[nodiscard]] double clamp_time(double t, double duration) noexcept;

// Seek target for current + delta, clamped to [0, duration].
[[nodiscard]] double seek_target(double current, double delta, double duration) noexcept;

// Map a playback time to an x within the seek bar [bar_x, bar_x + bar_w].
// duration <= 0 pins the position at bar_x.
[[nodiscard]] float time_to_bar_x(double t, double duration, float bar_x, float bar_w) noexcept;

// Inverse: map an x (e.g. a click/drag) to a playback time, clamped to [0, duration].
[[nodiscard]] double bar_x_to_time(float x, double duration, float bar_x, float bar_w) noexcept;

// Format seconds as "m:ss", or "h:mm:ss" past an hour. Negatives clamp to "0:00".
[[nodiscard]] std::string format_clock(double seconds);

class PlaybackModel {
public:
    explicit PlaybackModel(double duration_seconds) noexcept
        : duration_(duration_seconds > 0.0 ? duration_seconds : 0.0) {}

    [[nodiscard]] double position() const noexcept { return position_; }
    [[nodiscard]] double duration() const noexcept { return duration_; }
    [[nodiscard]] bool   playing()  const noexcept { return playing_; }
    [[nodiscard]] bool   at_end()   const noexcept;

    void set_playing(bool on) noexcept { playing_ = on; }
    void toggle() noexcept { playing_ = !playing_; }

    void seek_to(double t) noexcept { position_ = clamp_time(t, duration_); }
    void seek_by(double delta) noexcept { position_ = seek_target(position_, delta, duration_); }

    // Advance the clock by `dt` while playing; clamp to [0, duration] and
    // auto-pause on reaching the end.
    void tick(double dt) noexcept;

    // Should a frame with the given PTS be presented at the current clock?
    [[nodiscard]] static bool frame_due(double clock, double frame_pts) noexcept
    {
        return frame_pts <= clock + 1e-9;
    }

private:
    double duration_ = 0.0;
    double position_ = 0.0;
    bool   playing_  = false;
};

}  // namespace ui
