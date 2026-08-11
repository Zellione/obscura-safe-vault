#include "test_framework.h"

#include "ui/dual_layout.h"

using ui::dual_split;
using ui::pane_at;

TEST(dual_split_tiles_window_exactly)
{
    const auto s = dual_split(1200.0f, 800.0f);
    CHECK_EQ(s.left.x, 0.0f);
    CHECK_EQ(s.left.y, 0.0f);
    CHECK_EQ(s.left.h, 800.0f);
    CHECK_EQ(s.right.h, 800.0f);
    CHECK_EQ(s.divider.h, 800.0f);
    // left + divider + right tile the width exactly
    CHECK_EQ(s.divider.x, s.left.x + s.left.w);
    CHECK_EQ(s.right.x, s.divider.x + s.divider.w);
    CHECK_EQ(s.right.x + s.right.w, 1200.0f);
    // 50/50: pane widths differ by at most 1 px (odd widths)
    CHECK(s.left.w - s.right.w <= 1.0f && s.right.w - s.left.w <= 1.0f);
}

TEST(dual_split_divider_has_positive_width)
{
    const auto s = dual_split(1000.0f, 600.0f);
    CHECK(s.divider.w >= 1.0f);
}

TEST(dual_split_survives_tiny_window)
{
    // Shrinking while split must not produce negative pane widths.
    const auto s = dual_split(10.0f, 10.0f);
    CHECK(s.left.w >= 0.0f);
    CHECK(s.right.w >= 0.0f);
}

TEST(pane_at_resolves_left_right_and_divider)
{
    const auto s = dual_split(1200.0f, 800.0f);
    CHECK_EQ(pane_at(s, 10.0f), 0);
    CHECK_EQ(pane_at(s, 1190.0f), 1);
    CHECK_EQ(pane_at(s, s.divider.x), 0);            // divider left half -> left
    CHECK_EQ(pane_at(s, s.right.x), 1);
}

TEST(min_split_width_is_900)
{
    CHECK_EQ(ui::MIN_SPLIT_WIDTH, 900.0f);
}
