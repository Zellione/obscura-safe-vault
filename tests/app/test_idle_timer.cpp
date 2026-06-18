#include "test_framework.h"

#include "app/idle_timer.h"

TEST(idle_timer_accumulates_below_timeout)
{
    app::IdleTimer t(5.0);
    CHECK_FALSE(t.tick(1.0));
    CHECK_FALSE(t.tick(1.0));
    CHECK_EQ(t.elapsed(), 2.0);
}

TEST(idle_timer_fires_once_on_crossing_then_resets)
{
    app::IdleTimer t(5.0);
    CHECK_FALSE(t.tick(3.0));
    CHECK(t.tick(3.0));          // 6.0 >= 5.0 -> fire
    CHECK_EQ(t.elapsed(), 0.0);  // reset on fire
    CHECK_FALSE(t.tick(1.0));    // counting again from zero
}

TEST(idle_timer_reset_zeroes_elapsed)
{
    app::IdleTimer t(5.0);
    (void)t.tick(4.0);
    t.reset();
    CHECK_EQ(t.elapsed(), 0.0);
    CHECK_FALSE(t.tick(4.0));    // would have fired (8.0) without the reset
}

TEST(idle_timer_exact_timeout_fires)
{
    app::IdleTimer t(2.0);
    CHECK(t.tick(2.0));          // == timeout fires
}
