#include "media/volume_setting.h"

#include <algorithm>

namespace media {

namespace {
float g_volume = 1.0f;   // remembered playback level [0,1]; App seeds it at startup
}

float saved_volume() noexcept { return g_volume; }

void set_saved_volume(float v) noexcept { g_volume = std::clamp(v, 0.0f, 1.0f); }

} // namespace media
