#include "test_framework.h"

#include "media/autoplay_setting.h"

// The session autoplay-toggle global round-trips. It is process-wide shared state
// (like media::volume_setting), so restore the default at the end so ordering
// can't perturb other tests (e.g. VideoPlayback seeds its autoplay flag from it).
TEST(autoplay_setting_defaults_on_and_round_trips)
{
    const bool prev = media::saved_autoplay_enabled();
    CHECK(prev == true);                       // process default is ON
    media::set_saved_autoplay_enabled(false);
    CHECK(media::saved_autoplay_enabled() == false);
    media::set_saved_autoplay_enabled(prev);   // restore: process-global leaks across tests
}
