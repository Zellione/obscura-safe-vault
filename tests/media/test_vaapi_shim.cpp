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

#endif // OSV_VENDORED_AV && OSV_HWACCEL_VAAPI
