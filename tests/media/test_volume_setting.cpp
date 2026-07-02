#include "test_framework.h"

#include <cmath>

#include "media/volume_setting.h"

// The session volume global round-trips and clamps to [0,1]. It is process-wide
// shared state, so restore the default at the end so ordering can't perturb other
// tests (e.g. the AV video-playback test seeds its level from it).
TEST(volume_setting_get_set_and_clamp)
{
    media::set_saved_volume(0.4f);
    CHECK(std::abs(media::saved_volume() - 0.4f) < 1e-4f);

    media::set_saved_volume(2.0f);                      // over-range clamps to 1.0
    CHECK(std::abs(media::saved_volume() - 1.0f) < 1e-4f);

    media::set_saved_volume(-1.0f);                     // under-range clamps to 0.0
    CHECK(std::abs(media::saved_volume() - 0.0f) < 1e-4f);

    media::set_saved_volume(1.0f);                      // restore the default (shared global)
    CHECK(std::abs(media::saved_volume() - 1.0f) < 1e-4f);
}
