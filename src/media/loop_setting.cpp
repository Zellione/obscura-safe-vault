#include "media/loop_setting.h"

namespace media {

namespace {
// The remembered loop toggle. Held as a function-local static (like the
// active-theme slot in gfx/theme.cpp) so it stays mutable without a namespace-scope
// global variable.
bool& enabled_slot() noexcept
{
    static bool enabled = false;
    return enabled;
}
}  // namespace

bool saved_loop_enabled() noexcept { return enabled_slot(); }

void set_saved_loop_enabled(bool enabled) noexcept { enabled_slot() = enabled; }

} // namespace media
