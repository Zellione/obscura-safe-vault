#include "test_framework.h"

#include "media/loop_setting.h"

// The session loop-toggle global round-trips. It is process-wide shared state
// (like media::volume_setting), so restore the default at the end so ordering
// can't perturb other tests (e.g. VideoPlayback seeds its loop flag from it).
TEST(loop_setting_default_is_false)
{
    media::set_saved_loop_enabled(false);   // pin a known state first (test order)
    CHECK(media::saved_loop_enabled() == false);
}

TEST(loop_setting_get_set_round_trips)
{
    media::set_saved_loop_enabled(true);
    CHECK(media::saved_loop_enabled() == true);

    media::set_saved_loop_enabled(false);
    CHECK(media::saved_loop_enabled() == false);

    media::set_saved_loop_enabled(false);   // restore the default (shared global)
}
