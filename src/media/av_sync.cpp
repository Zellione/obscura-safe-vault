#include "media/av_sync.h"

namespace media {

using enum FrameAction;

FrameAction decide(double audio_clock, double frame_pts,
                   double ahead, double behind) noexcept
{
    if (frame_pts > audio_clock + ahead)  return Hold;
    if (frame_pts < audio_clock - behind) return Drop;
    return Present;
}

double audio_clock(double base_seconds, uint64_t samples_consumed,
                   int sample_rate) noexcept
{
    if (sample_rate <= 0) return base_seconds;
    return base_seconds + static_cast<double>(samples_consumed) / sample_rate;
}

float clamp_volume(float v) noexcept
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

float effective_gain(float volume, bool muted) noexcept
{
    return muted ? 0.0f : clamp_volume(volume);
}

}  // namespace media
