#include "test_framework.h"

#include "ui/grid_layout.h"

// Test grid_visible_range with various scenarios

TEST(grid_visible_range_empty_grid)
{
    // Empty grid: always returns {0, -1} (empty range marker).
    auto r = ui::grid_visible_range(0.0f, 188.0f, 16.0f, 160.0f, 720.0f, 4, 0);
    CHECK_EQ(r.first, 0);
    CHECK_EQ(r.second, -1);
}

TEST(grid_visible_range_single_item)
{
    // Single item in viewport: should be visible with margins applied but clamped.
    auto r = ui::grid_visible_range(0.0f, 100.0f, 10.0f, 50.0f, 500.0f, 4, 1);
    CHECK_EQ(r.first, 0);
    CHECK_EQ(r.second, 0);  // only one item, so end_index is 0
}

TEST(grid_visible_range_full_viewport)
{
    // Grid with cells 188x188, gap 16, origin_y=160, viewport=720
    // Row pitch = 188 + 16 = 204
    // Row 0: [160, 348), Row 1: [364, 552), Row 2: [568, 756), Row 3: [772, 960)
    // Viewport: [0, 720)
    // Visible rows before margin: 0, 1, 2 (row 3 starts at 772 > 720)
    // With margin: -1 (clamped to 0), 3 (clamped to min)
    // So rows 0, 1, 2, 3
    // With 4 columns: indices 0..15 (rows 0-3, where row 3 has only items 12-15)
    auto r = ui::grid_visible_range(0.0f, 188.0f, 16.0f, 160.0f, 720.0f, 4, 16);
    CHECK_EQ(r.first, 0);
    CHECK_EQ(r.second, 15);
}

TEST(grid_visible_range_mid_scroll)
{
    // Viewport scrolled so only middle rows are visible.
    // Same grid (188+16=204 pitch), viewport=[400, 1120)
    // Row 1: [364, 552) overlaps [400, 1120) ✓
    // Row 2: [568, 756) overlaps ✓
    // Row 3: [772, 960) overlaps ✓
    // Row 4: [976, 1164) overlaps (976 < 1120 AND 976 > 400) ✓
    // Row 5: [1180, 1368) top is >= 1120 ✗
    // Visible: rows 1-4, with margin: 0-5
    // Indices: 0..23 (6 rows × 4 cols, clamped to actual count)
    auto r = ui::grid_visible_range(400.0f, 188.0f, 16.0f, 160.0f, 720.0f, 4, 32);
    CHECK_EQ(r.first, 0);   // margin above row 1 brings us to row 0
    CHECK_EQ(r.second, 23);  // margin below row 4 = row 5, which is 20-23 (indices)
}

TEST(grid_visible_range_scrolled_to_end)
{
    // Scroll so we see the last items.
    // Viewport scrolled to show the end of content (rows 2-4, near the end).
    // 20 items = 5 rows (4 columns), height = 5*204 = 1020
    // Row 2: [568, 756), Row 3: [772, 960), Row 4: [976, 1164)
    // To show rows 2-4 (excluding rows 0-1), use scroll_offset = 560
    // viewport = [560, 1280)
    // Row 0: [160, 348) doesn't overlap (348 < 560)
    // Row 1: [364, 552) doesn't overlap (552 < 560)
    // Row 2: [568, 756) overlaps
    // Row 3: [772, 960) overlaps
    // Row 4: [976, 1164) overlaps
    // Visible rows: 2-4, with margin: 1-4
    // Row 1 starts at index 4, row 4 ends at index 19
    auto r = ui::grid_visible_range(560.0f, 188.0f, 16.0f, 160.0f, 720.0f, 4, 20);
    CHECK_EQ(r.first, 4);
    CHECK_EQ(r.second, 19);
}

TEST(grid_visible_range_single_column_like_list)
{
    // cols=1: grid behaves like a list.
    // 10 items, each row is one item.
    // cell=100, gap=10, pitch=110, origin_y=50
    // Row 0: [50, 150), Row 1: [160, 260), ... Row 5: [600, 700), Row 6: [710, 810)
    // viewport=[0, 500)
    // Visible: rows 0-4 (row 5 at 600 is partially visible, row 6 at 710 is not)
    // With margin: -1 (→ 0) to 5
    // Indices: 0-5
    auto r = ui::grid_visible_range(0.0f, 100.0f, 10.0f, 50.0f, 500.0f, 1, 10);
    CHECK_EQ(r.first, 0);
    CHECK_EQ(r.second, 5);
}

TEST(grid_visible_range_viewport_taller_than_content)
{
    // Viewport is taller than all content.
    // 4 items, 2 columns, 2 rows.
    // Row height = 100 + 10 = 110, total = 220.
    // viewport = 500 (much taller).
    // All rows visible, with margin expands but clamps.
    auto r = ui::grid_visible_range(0.0f, 100.0f, 10.0f, 50.0f, 500.0f, 2, 4);
    CHECK_EQ(r.first, 0);
    CHECK_EQ(r.second, 3);  // all 4 items (2 rows × 2 cols)
}

TEST(grid_visible_range_zero_scroll_offset_columns_zero)
{
    // Edge case: columns = 0 or negative.
    auto r = ui::grid_visible_range(0.0f, 100.0f, 10.0f, 50.0f, 500.0f, 0, 10);
    CHECK_EQ(r.first, 0);
    CHECK_EQ(r.second, -1);  // empty range
}

TEST(grid_visible_range_negative_scroll)
{
    // Scroll offset negative (scrolled up, though unusual).
    // Viewport = [-100, 620)
    // Visible rows should still compute correctly.
    auto r = ui::grid_visible_range(-100.0f, 188.0f, 16.0f, 160.0f, 720.0f, 4, 16);
    // With scroll_offset = -100:
    // Row 0: [160, 348) overlaps [-100, 620) ✓
    // Row 1: [364, 552) overlaps ✓
    // Row 2: [568, 756) overlaps (568 < 620 AND 756 > -100) ✓
    // Row 3: [772, 960) top=772 >= 620 ✗
    // Visible: 0-2, with margin: -1 (→ 0) to 3
    // Indices: 0..15
    CHECK_EQ(r.first, 0);
    CHECK_EQ(r.second, 15);
}

TEST(grid_visible_range_partial_last_row)
{
    // Grid with 10 items, 4 columns: 3 rows (row 2 has only 2 items: indices 8-9).
    // Check margin doesn't go past item_count.
    auto r = ui::grid_visible_range(0.0f, 100.0f, 10.0f, 50.0f, 500.0f, 4, 10);
    // Rows: 0 [50, 150), 1 [160, 260), 2 [270, 370)
    // Viewport: [0, 500)
    // All rows visible, margin would add row -1 and 3, both clamped.
    // Indices: 0..9
    CHECK_EQ(r.first, 0);
    CHECK_EQ(r.second, 9);
}

// --- List tests ---

TEST(list_visible_range_empty)
{
    // Empty list.
    auto r = ui::list_visible_range(0.0f, 44.0f, 30.0f, 500.0f, 0);
    CHECK_EQ(r.first, 0);
    CHECK_EQ(r.second, -1);
}

TEST(list_visible_range_single_item)
{
    // Single item in list.
    auto r = ui::list_visible_range(0.0f, 44.0f, 30.0f, 500.0f, 1);
    CHECK_EQ(r.first, 0);
    CHECK_EQ(r.second, 0);
}

TEST(list_visible_range_full_viewport)
{
    // List with rows at:
    // Row 0: [30, 74), Row 1: [74, 118), Row 2: [118, 162), ...
    // Viewport: [0, 500)
    // ~11 rows fit: rows 0-10 (30 + 11*44 = 514 > 500)
    // Row 10: [30 + 10*44, 30 + 11*44) = [470, 514) overlaps [0, 500) ✓
    // Row 11: [514, 558) doesn't overlap (514 >= 500) ✗
    // Visible: 0-10, margin: -1 (→ 0) to 11 (clamped to item_count-1 = 19)
    auto r = ui::list_visible_range(0.0f, 44.0f, 30.0f, 500.0f, 20);
    CHECK_EQ(r.first, 0);
    CHECK_EQ(r.second, 11);  // rows 0-11 visible with margins, but clamped at 19
}

TEST(list_visible_range_mid_scroll)
{
    // Scrolled to show middle rows.
    // viewport = [200, 700), row_height = 44, header_offset = 30
    // Row 3: [30 + 3*44, 30 + 4*44) = [162, 206) overlaps ✓
    // Row 4: [206, 250) overlaps ✓
    // ... Row 15: [30 + 15*44, 30 + 16*44) = [690, 734) overlaps ✓
    // Row 16: [734, 778) top >= 700 ✗
    // Visible: rows 3-15, margin: 2-16 (clamped)
    auto r = ui::list_visible_range(200.0f, 44.0f, 30.0f, 500.0f, 20);
    CHECK_EQ(r.first, 2);   // margin above row 3
    CHECK_EQ(r.second, 16);  // margin below row 15
}

TEST(list_visible_range_scrolled_to_end)
{
    // Scrolled so we see the last items.
    // 20 items, heights at: 30 + i*44 for i=0..19
    // Last row (19): [30 + 19*44, 30 + 20*44) = [866, 910)
    // If viewport bottom = 910, viewport_height = 500:
    // scroll_offset = 910 - 500 = 410
    // viewport = [410, 910)
    // Row 8: [30 + 8*44, 30 + 9*44) = [382, 426) overlaps ✓
    // ... Row 19: [866, 910) overlaps ✓
    // With margin: 7-19 (19 clamped as it's the last)
    auto r = ui::list_visible_range(410.0f, 44.0f, 30.0f, 500.0f, 20);
    CHECK_EQ(r.first, 7);    // margin above row 8
    CHECK_EQ(r.second, 19);  // last item
}

TEST(list_visible_range_viewport_taller_than_content)
{
    // Viewport taller than all content.
    // 10 items, total height = 30 + 10*44 = 470
    // viewport = 1000
    // All items visible, margins clamped.
    auto r = ui::list_visible_range(0.0f, 44.0f, 30.0f, 1000.0f, 10);
    CHECK_EQ(r.first, 0);
    CHECK_EQ(r.second, 9);
}

TEST(list_visible_range_zero_row_height)
{
    // Edge case: row_height = 0 (invalid, but should handle gracefully).
    auto r = ui::list_visible_range(0.0f, 0.0f, 30.0f, 500.0f, 10);
    CHECK_EQ(r.first, 0);
    CHECK_EQ(r.second, -1);  // empty
}

TEST(list_visible_range_no_overlap)
{
    // Viewport doesn't overlap any rows (scrolled past the end or before start).
    // header_offset = 1000, viewport = [0, 500) — rows start at 1000, so no overlap.
    auto r = ui::list_visible_range(0.0f, 44.0f, 1000.0f, 500.0f, 10);
    CHECK_EQ(r.first, 0);
    CHECK_EQ(r.second, -1);  // empty
}

TEST(list_visible_range_margin_clamping)
{
    // Check that margins don't exceed bounds.
    // 3 items at rows 0, 1, 2.
    // Viewport shows all: rows 0-2
    // Margin would add -1 and 3, both clamped: final 0-2
    auto r = ui::list_visible_range(0.0f, 100.0f, 50.0f, 1000.0f, 3);
    CHECK_EQ(r.first, 0);
    CHECK_EQ(r.second, 2);
}
