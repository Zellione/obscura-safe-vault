#include "media/volume_setting.h"

#include <algorithm>

namespace media {

namespace {
// The remembered playback level [0,1]. Held as a function-local static (like the
// active-theme slot in gfx/theme.cpp) so it stays mutable without a namespace-scope
// global variable. App seeds it at startup.
float& level_slot() noexcept
{
    static float level = 1.0f;
    return level;
}
}  // namespace

float saved_volume() noexcept { return level_slot(); }

void set_saved_volume(float v) noexcept { level_slot() = std::clamp(v, 0.0f, 1.0f); }

} // namespace media
