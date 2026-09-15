#include "media/hwaccel_setting.h"

namespace media {

namespace {
// Function-local statics (cpp:S5421) so neither value is a namespace-scope
// global. Both default to the safe-for-new-clips state — hardware on,
// software forced off — exactly the behaviour a fresh install gets when
// the pref file is missing.
bool& enable_hardware_slot() noexcept
{
    static bool enabled = true;   // hardware on by default
    return enabled;
}
bool& force_software_slot() noexcept
{
    static bool forced = false;   // software-forced off by default
    return forced;
}
}  // namespace

bool enable_hardware_decode() noexcept { return enable_hardware_slot(); }
void set_enable_hardware_decode(bool enabled) noexcept { enable_hardware_slot() = enabled; }

bool force_software_decode() noexcept { return force_software_slot(); }
void set_force_software_decode(bool enabled) noexcept { force_software_slot() = enabled; }

} // namespace media
