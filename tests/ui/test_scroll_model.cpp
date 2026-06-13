#include "test_framework.h"

#include "ui/scroll_model.h"

namespace {
ui::ScrollModel make() { return ui::ScrollModel({100.0f, 200.0f, 300.0f}, 150.0f); }
}

TEST(scroll_model_geometry)
{
    const ui::ScrollModel m = make();
    CHECK_EQ(m.count(), 3);
    CHECK_EQ(m.total_height(), 600.0f);
    CHECK_EQ(m.max_scroll(), 450.0f);          // 600 - 150
}

TEST(scroll_model_image_tops_are_prefix_sums)
{
    const ui::ScrollModel m = make();
    CHECK_EQ(m.image_top(0), 0.0f);
    CHECK_EQ(m.image_top(1), 100.0f);
    CHECK_EQ(m.image_top(2), 300.0f);
    CHECK_EQ(m.image_top(3), 600.0f);          // past-the-end clamps to total
}

TEST(scroll_model_clamps_scroll)
{
    const ui::ScrollModel m = make();
    CHECK_EQ(m.clamp_scroll(-10.0f), 0.0f);
    CHECK_EQ(m.clamp_scroll(100.0f), 100.0f);
    CHECK_EQ(m.clamp_scroll(9999.0f), 450.0f);
}

TEST(scroll_model_active_index_follows_viewport_center)
{
    const ui::ScrollModel m = make();
    CHECK_EQ(m.active_index(0.0f),   0);   // centre 75   -> image 0 [0,100)
    CHECK_EQ(m.active_index(100.0f), 1);   // centre 175  -> image 1 [100,300)
    CHECK_EQ(m.active_index(300.0f), 2);   // centre 375  -> image 2 [300,600)
    CHECK_EQ(m.active_index(450.0f), 2);   // centre 525  -> clamped to last
}

TEST(scroll_model_visible_range_is_intersecting_images)
{
    const ui::ScrollModel m = make();
    auto a = m.visible_range(0.0f);        // [0,150) -> images 0,1
    CHECK_EQ(a.first, 0);
    CHECK_EQ(a.second, 1);

    auto b = m.visible_range(250.0f);      // [250,400) -> images 1,2
    CHECK_EQ(b.first, 1);
    CHECK_EQ(b.second, 2);
}

TEST(scroll_model_empty_is_safe)
{
    const ui::ScrollModel m({}, 150.0f);
    CHECK_EQ(m.count(), 0);
    CHECK_EQ(m.total_height(), 0.0f);
    CHECK_EQ(m.max_scroll(), 0.0f);
    CHECK_EQ(m.active_index(0.0f), 0);
    auto v = m.visible_range(0.0f);
    CHECK_EQ(v.first, 0);
    CHECK_EQ(v.second, -1);
}
