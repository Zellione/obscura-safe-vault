// tests/app/test_keep_unlocked_badge.cpp
#include "test_framework.h"

#include "app/keep_unlocked_badge.h"

TEST(badge_hidden_when_auto_lock_is_on)
{
    CHECK_FALSE(app::should_show_badge(/*keep_unlocked=*/false, /*seconds_since_toggle=*/0.0, 10.0));
}

TEST(badge_shown_right_after_the_toggle)
{
    CHECK(app::should_show_badge(true, 0.0, 10.0));
}

TEST(badge_shown_just_before_the_window_elapses)
{
    CHECK(app::should_show_badge(true, 9.9, 10.0));
}

TEST(badge_hidden_once_the_window_elapses)
{
    CHECK_FALSE(app::should_show_badge(true, 10.0, 10.0));
    CHECK_FALSE(app::should_show_badge(true, 60.0, 10.0));
}
