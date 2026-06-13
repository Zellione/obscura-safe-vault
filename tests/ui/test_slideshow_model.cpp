#include "test_framework.h"

#include <set>

#include "ui/slideshow_model.h"

namespace {
// A 3-image slideshow starting at image 0, dwell 2s, no shuffle.
ui::SlideshowModel make() { return ui::SlideshowModel(3, 0, 2.0); }
}

TEST(slideshow_model_defaults)
{
    const ui::SlideshowModel m = make();
    CHECK_EQ(m.count(), 3);
    CHECK_EQ(m.index(), 0);
    CHECK_TRUE(m.running());                  // starts playing
    CHECK_EQ(m.dwell(), 2.0);
    CHECK_EQ(m.prev_index(), -1);             // no transition yet
    CHECK_EQ(m.fade_progress(), 1.0);         // fully showing current
}

TEST(slideshow_model_dwell_clamps)
{
    ui::SlideshowModel m = make();
    m.set_dwell(0.0);
    CHECK_EQ(m.dwell(), ui::SLIDESHOW_DWELL_MIN);
    m.set_dwell(9999.0);
    CHECK_EQ(m.dwell(), ui::SLIDESHOW_DWELL_MAX);

    m.set_dwell(4.0);
    m.adjust_dwell(-1.0);
    CHECK_EQ(m.dwell(), 3.0);
    m.adjust_dwell(1000.0);
    CHECK_EQ(m.dwell(), ui::SLIDESHOW_DWELL_MAX);
}

TEST(slideshow_model_tick_advances_at_dwell)
{
    ui::SlideshowModel m = make();             // dwell 2.0
    CHECK_FALSE(m.tick(1.0));                   // 1s elapsed: not yet
    CHECK_EQ(m.index(), 0);
    CHECK_TRUE(m.tick(1.0));                    // 2s reached: advance
    CHECK_EQ(m.index(), 1);
}

TEST(slideshow_model_wraps_at_end)
{
    ui::SlideshowModel m(3, 2, 2.0);           // start on the last image
    m.advance(1);
    CHECK_EQ(m.index(), 0);                     // wraps to the first
    m.advance(-1);
    CHECK_EQ(m.index(), 2);                     // and back
}

TEST(slideshow_model_pause_halts_timer)
{
    ui::SlideshowModel m = make();
    m.set_running(false);
    CHECK_FALSE(m.running());
    CHECK_FALSE(m.tick(100.0));                 // paused: timer never fires
    CHECK_EQ(m.index(), 0);
    m.toggle();
    CHECK_TRUE(m.running());
}

TEST(slideshow_model_fade_progress_clamps)
{
    ui::SlideshowModel m = make();
    m.set_running(false);                      // isolate the fade from the dwell timer
    m.advance(1);                              // begin a cross-fade 0 -> 2
    CHECK_EQ(m.prev_index(), 0);
    CHECK_EQ(m.index(), 1);
    CHECK_EQ(m.fade_progress(), 0.0);

    m.tick(ui::SLIDESHOW_FADE * 0.5);
    CHECK_EQ(m.fade_progress(), 0.5);
    m.tick(ui::SLIDESHOW_FADE);                // overshoot clamps to 1 and ends
    CHECK_EQ(m.fade_progress(), 1.0);
    CHECK_EQ(m.prev_index(), -1);
}

TEST(slideshow_model_shuffle_visits_each_once_per_cycle)
{
    ui::SlideshowModel m(5, 0, 2.0, /*shuffle=*/true, /*seed=*/12345u);
    std::set<int> seen{m.index()};
    for (int i = 0; i < 4; ++i) {              // one full cycle = count advances
        m.advance(1);
        seen.insert(m.index());
    }
    CHECK_EQ(static_cast<int>(seen.size()), 5);  // every index exactly once
    CHECK_TRUE(seen.count(0) && seen.count(4));
}

TEST(slideshow_model_empty_is_safe)
{
    ui::SlideshowModel m(0, 0, 2.0);
    CHECK_EQ(m.count(), 0);
    CHECK_FALSE(m.tick(100.0));                 // nothing to advance, no crash
    m.advance(1);
    CHECK_EQ(m.fade_progress(), 1.0);
}

TEST(slideshow_model_clamp_dwell_helper)
{
    CHECK_EQ(ui::clamp_dwell(-5.0), ui::SLIDESHOW_DWELL_MIN);
    CHECK_EQ(ui::clamp_dwell(1000.0), ui::SLIDESHOW_DWELL_MAX);
    CHECK_EQ(ui::clamp_dwell(4.0), 4.0);
}
