#pragma once
#include <cstddef>
#include <cstdint>

namespace media {

enum class FrameAction { Present, Hold, Drop };

// Sync tolerances (seconds). A video frame whose PTS is more than `ahead` past
// the audio clock is held (too early); more than `behind` before it is dropped
// (too late); otherwise presented.
inline constexpr double AV_SYNC_AHEAD  = 0.010;
inline constexpr double AV_SYNC_BEHIND = 0.040;

// Phase 85: audio-side seek resolution. The video decode worker skips decoded
// frames below the seek target, but audio would otherwise play from the
// keyframe the demuxer landed on — audibly replaying pre-target sound. During
// a seek-resolve the host asks, per decoded audio frame: Drop it (it ends at
// or before the target — nothing audible at/after the target), or Start — feed
// it and re-base the audio clock on this frame's actual pts. Degenerate input
// (no samples / bad rate) fails open to Start so audio can never be dropped
// forever.
enum class AudioSeekSkip : uint8_t { Drop, Start };
[[nodiscard]] AudioSeekSkip audio_seek_skip(double frame_pts, size_t frame_count,
                                            int sample_rate, double target) noexcept;

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
