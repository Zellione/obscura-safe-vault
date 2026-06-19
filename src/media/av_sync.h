#pragma once
#include <cstdint>

namespace media {

enum class FrameAction { Present, Hold, Drop };

// Sync tolerances (seconds). A video frame whose PTS is more than `ahead` past
// the audio clock is held (too early); more than `behind` before it is dropped
// (too late); otherwise presented.
inline constexpr double AV_SYNC_AHEAD  = 0.010;
inline constexpr double AV_SYNC_BEHIND = 0.040;

[[nodiscard]] FrameAction decide(double audio_clock, double frame_pts,
                                 double ahead  = AV_SYNC_AHEAD,
                                 double behind = AV_SYNC_BEHIND) noexcept;

// Audio clock in seconds = base (last seek target) + consumed / rate.
// rate <= 0 collapses to `base_seconds`.
[[nodiscard]] double audio_clock(double base_seconds, uint64_t samples_consumed,
                                 int sample_rate) noexcept;

[[nodiscard]] float clamp_volume(float v) noexcept;             // -> [0,1]
[[nodiscard]] float effective_gain(float volume, bool muted) noexcept;

}  // namespace media
