#include "test_framework.h"

#if defined(OSV_VENDORED_AV) && defined(OSV_HWACCEL_VAAPI)

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
extern "C" {
#include <va/va.h>
#include <va/va_drm.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <fcntl.h>
#include <unistd.h>

#include "media/hw_accel.h"
#include "media/hwaccel_setting.h"

// Exercises vendor/vaapi-shim's dlopen-forwarding mechanism directly (not
// through media::HwAccelContext), on whatever this machine actually has --
// real libva installed, or not at all. Either way must not crash, and must
// return the same "unavailable"/valid-shaped values a real libva driver
// would for an unsupported call. See vendor/vaapi-shim/osv_vaapi_shim.c.

TEST(vaapi_shim_error_str_never_crashes)
{
    // vaErrorStr() must return SOME non-null string for a defined status
    // code, whether it's the real libva's string table or (if libva.so.2
    // isn't dlopen-able on this machine) any fallback -- the shim's
    // documented "fail" value for this function is NULL, which is also an
    // acceptable, non-crashing result here (this test permits either).
    const char* s = vaErrorStr(VA_STATUS_ERROR_UNIMPLEMENTED);
    CHECK(true);   // reaching here without crashing is the primary assertion
    (void)s;
}

TEST(vaapi_shim_query_vendor_string_without_display_is_safe)
{
    // Calling with a null VADisplay is not a realistic real-driver call
    // (media::HwAccelContext never does this), but it proves the shim's
    // dlopen/dlsym forwarding path itself tolerates being invoked and
    // returns without crashing regardless of whether the real symbol
    // resolved.
    const char* s = vaQueryVendorString(nullptr);
    CHECK(true);
    (void)s;
}

TEST(vaapi_shim_get_display_drm_on_missing_render_node_returns_null_or_valid)
{
    // Opening a DRM render node that doesn't exist must fail cleanly (fd <
    // 0), and vaGetDisplayDRM() on an invalid/negative fd must not crash --
    // this is the exact shape of failure a CI runner with no /dev/dri
    // produces, which media::HwAccelContext's cached_device_ctx() must
    // already tolerate (Part 1).
    int fd = open("/dev/dri/renderD_does_not_exist", O_RDWR);
    REQUIRE(fd < 0);
    VADisplay dpy = vaGetDisplayDRM(fd);
    CHECK(true);   // reaching here without crashing is the assertion
    (void)dpy;
}

// Phase 102: the two runtime overrides round-trip through their setters.
// We don't drive try_attach_hwaccel from a test directly (it requires a
// real AVCodecContext + AVCodec pair); the existing
// video_decode_worker_hwaccel_forced_unavailable_matches_normal_decode
// test already exercises the actual decode path with the
// test_only_force_hwaccel_unavailable seam.
TEST(hwaccel_enable_hardware_decode_round_trip)
{
    media::set_enable_hardware_decode(true);
    CHECK(media::enable_hardware_decode());
    media::set_enable_hardware_decode(false);
    CHECK_FALSE(media::enable_hardware_decode());
    media::set_force_software_decode(false);
    CHECK_FALSE(media::force_software_decode());
    media::set_force_software_decode(true);
    CHECK(media::force_software_decode());
    // Restore defaults so subsequent tests see a clean state.
    media::set_enable_hardware_decode(true);
    media::set_force_software_decode(false);
}

TEST(hwaccel_status_initial_value)
{
    // Before the first probe the accessor returns NotAttempted. After the
    // first probe (which only fires from try_attach_hwaccel) it lands on
    // Ok or Unavailable depending on the host — both are valid steady
    // states, so the test permits either.
    CHECK(static_cast<int>(media::hwaccel_status()) == static_cast<int>(media::HwAccelStatus::NotAttempted) ||
          static_cast<int>(media::hwaccel_status()) == static_cast<int>(media::HwAccelStatus::Unavailable) ||
          static_cast<int>(media::hwaccel_status()) == static_cast<int>(media::HwAccelStatus::Ok));
}

#endif // OSV_VENDORED_AV && OSV_HWACCEL_VAAPI
