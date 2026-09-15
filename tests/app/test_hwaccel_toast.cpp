#include "test_framework.h"

#include "app/hwaccel_toast.h"

TEST(hwaccel_toast_shows_when_text_set_and_within_window)
{
    // Empty text + within window: hidden (the "no toast at all" path).
    CHECK_FALSE(app::should_show_hwaccel_toast("", 0.0, 2.5));
    CHECK_FALSE(app::should_show_hwaccel_toast(nullptr, 0.0, 2.5));

    // Text set, within window: visible.
    CHECK(app::should_show_hwaccel_toast("Hardware decode: ON", 0.0, 2.5));
    CHECK(app::should_show_hwaccel_toast("Force software decode: OFF", 1.0, 2.5));
    CHECK(app::should_show_hwaccel_toast("X", 2.49, 2.5));
}

TEST(hwaccel_toast_hides_after_window)
{
    // Text set but past the window: hidden.
    CHECK_FALSE(app::should_show_hwaccel_toast("Hardware decode: ON", 2.5, 2.5));
    CHECK_FALSE(app::should_show_hwaccel_toast("Hardware decode: ON", 5.0, 2.5));
}
