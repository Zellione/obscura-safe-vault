#include "media/autoplay_setting.h"

namespace media {

namespace {
// The remembered autoplay toggle. Held as a function-local static (like the
// active-theme slot in gfx/theme.cpp) so it stays mutable without a namespace-scope
// global variable.
bool& enabled_slot() noexcept
{
    static bool enabled = true;  // default ON (unlike loop_setting which defaults OFF)
    return enabled;
}
}  // namespace

bool saved_autoplay_enabled() noexcept { return enabled_slot(); }

void set_saved_autoplay_enabled(bool enabled) noexcept { enabled_slot() = enabled; }

} // namespace media
