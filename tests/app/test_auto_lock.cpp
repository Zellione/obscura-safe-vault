#include "test_framework.h"

#include "app/auto_lock.h"
#include "app/idle_timer.h"

TEST(auto_lock_false_and_resets_when_no_active_vault)
{
    app::IdleTimer t(5.0);
    (void)t.tick(3.0);   // accumulate some elapsed time first
    CHECK_FALSE(app::should_auto_lock(/*has_active=*/false, /*blocks_idle_lock=*/false,
                                      /*keep_unlocked=*/false, t, 1.0));
    CHECK_EQ(t.elapsed(), 0.0);   // suppressed ticks reset instead of accumulating
}

TEST(auto_lock_false_and_resets_when_screen_blocks_idle_lock)
{
    app::IdleTimer t(5.0);
    (void)t.tick(3.0);
    CHECK_FALSE(app::should_auto_lock(/*has_active=*/true, /*blocks_idle_lock=*/true,
                                      /*keep_unlocked=*/false, t, 1.0));
    CHECK_EQ(t.elapsed(), 0.0);
}

TEST(auto_lock_false_and_resets_when_keep_unlocked)
{
    app::IdleTimer t(5.0);
    CHECK_FALSE(app::should_auto_lock(/*has_active=*/true, /*blocks_idle_lock=*/false,
                                      /*keep_unlocked=*/true, t, 100.0));  // far past timeout
    CHECK_EQ(t.elapsed(), 0.0);
}

TEST(auto_lock_fires_when_timeout_reached_and_nothing_suppresses)
{
    app::IdleTimer t(5.0);
    CHECK_FALSE(app::should_auto_lock(true, false, false, t, 3.0));
    CHECK(app::should_auto_lock(true, false, false, t, 3.0));   // 6.0 >= 5.0 -> fire
    CHECK_EQ(t.elapsed(), 0.0);                                  // fired tick also resets
}

TEST(auto_lock_disabling_keep_unlocked_starts_counting_fresh)
{
    app::IdleTimer t(5.0);
    // While suppressed, a huge dt must not leave the timer primed to fire
    // the instant suppression is lifted.
    CHECK_FALSE(app::should_auto_lock(true, false, true, t, 1000.0));
    CHECK_FALSE(app::should_auto_lock(true, false, false, t, 1.0));
    CHECK_EQ(t.elapsed(), 1.0);
}
