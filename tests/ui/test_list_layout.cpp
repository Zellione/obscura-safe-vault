// Phase 56: the vertical-list geometry shared by the advanced-search screen, the
// saved-search panel, the search result view, the search overlay and the tag
// editor. Two of those advanced by less than the glyph height (25.5 px and
// 0.9 * 30 px under a 28 px font), so their rows' ink overlapped. Both
// properties that forbid it — rows never overlap, and a row counted visible is
// fully above the cut-off — are asserted here.

#include "test_framework.h"

#include "ui/list_layout.h"
#include "ui/text_metrics.h"

namespace {
constexpr float FONT_PX = 28.0f;

ui::ListMetrics metrics(float gap = 0.0f)
{
    return {.top = 100.0f, .row_h = ui::line_pitch(FONT_PX), .gap = gap};
}
} // namespace

TEST(list_rows_never_overlap_under_the_live_font)
{
    const ui::ListMetrics m = metrics();
    for (int i = 1; i < 20; ++i) {
        CHECK(ui::list_row_y(m, i - 1) + FONT_PX <= ui::list_row_y(m, i));
    }
}

TEST(list_rows_advance_by_the_row_height_plus_gap)
{
    const ui::ListMetrics m = metrics(6.0f);
    CHECK_EQ(ui::list_row_y(m, 0), 100.0f);
    CHECK_EQ(ui::list_row_y(m, 1), 100.0f + m.row_h + 6.0f);
    CHECK_EQ(ui::list_row_y(m, 3), 100.0f + 3.0f * (m.row_h + 6.0f));
}

TEST(every_visible_row_fits_entirely_above_the_cut_off)
{
    const ui::ListMetrics m = metrics();
    for (float bottom = 100.0f; bottom <= 900.0f; bottom += 3.0f) {
        const int n = ui::list_visible_rows(m, bottom);
        if (n <= 0) continue;
        CHECK(ui::list_row_y(m, n - 1) + m.row_h <= bottom);
    }
}

TEST(visible_row_count_is_maximal)
{
    const ui::ListMetrics m = metrics();
    for (float bottom = 100.0f; bottom <= 900.0f; bottom += 3.0f) {
        const int n = ui::list_visible_rows(m, bottom);
        CHECK(ui::list_row_y(m, n) + m.row_h > bottom);   // one more would not fit
    }
}

TEST(a_list_with_no_room_shows_no_rows)
{
    const ui::ListMetrics m = metrics();
    CHECK_EQ(ui::list_visible_rows(m, 100.0f), 0);
    CHECK_EQ(ui::list_visible_rows(m, 50.0f), 0);      // bottom above top
}

TEST(rows_fit_is_the_visible_count_capped_by_the_item_count)
{
    const ui::ListMetrics m = metrics();
    const int visible = ui::list_visible_rows(m, 600.0f);
    CHECK(visible > 2);
    CHECK_EQ(ui::list_rows_fit(m, 600.0f, 2), 2);
    CHECK_EQ(ui::list_rows_fit(m, 600.0f, 1000), visible);
    CHECK_EQ(ui::list_rows_fit(m, 600.0f, 0), 0);
}

TEST(a_degenerate_row_height_yields_no_rows_rather_than_dividing_by_zero)
{
    const ui::ListMetrics m{.top = 100.0f, .row_h = 0.0f, .gap = 0.0f};
    CHECK_EQ(ui::list_visible_rows(m, 900.0f), 0);
}

TEST(list_clamp_scroll_returns_zero_when_content_fits)
{
    // 10 rows at 28px each + 1 header = 11*28 = 308px. Available = 720-100 = 620px.
    // Content fits entirely -> scroll should always be 0.
    const float row_h = ui::line_pitch(FONT_PX);
    const float scroll = ui::list_clamp_scroll(100.0f, 10, row_h, 100.0f, 720.0f);
    CHECK_EQ(scroll, 0.0f);
}

TEST(list_clamp_scroll_clamps_to_valid_range)
{
    // 100 rows, available space limited. Calculate expected max_offset.
    const float row_h = ui::line_pitch(FONT_PX);
    const float top = 100.0f;
    const float max_h = 400.0f;           // only 300px available
    const int count = 100;
    const float content_h = (count + 1.0f) * row_h;  // header + 100 rows
    const float max_offset = content_h - (max_h - top);

    // Test values get clamped to [0, max_offset].
    CHECK_EQ(ui::list_clamp_scroll(-10.0f, count, row_h, top, max_h), 0.0f);
    CHECK_EQ(ui::list_clamp_scroll(max_offset + 10.0f, count, row_h, top, max_h), max_offset);
    CHECK_EQ(ui::list_clamp_scroll(max_offset * 0.5f, count, row_h, top, max_h), max_offset * 0.5f);
}

TEST(list_clamp_scroll_with_zero_rows)
{
    const float row_h = ui::line_pitch(FONT_PX);
    const float scroll = ui::list_clamp_scroll(50.0f, 0, row_h, 100.0f, 500.0f);
    CHECK_EQ(scroll, 0.0f);  // No rows = no scroll
}

TEST(list_clamp_scroll_with_degenerate_dimensions)
{
    // top >= max_h -> no available space -> no scroll
    CHECK_EQ(ui::list_clamp_scroll(100.0f, 10, 28.0f, 500.0f, 400.0f), 0.0f);
    // zero row_h -> no scroll
    CHECK_EQ(ui::list_clamp_scroll(100.0f, 10, 0.0f, 100.0f, 500.0f), 0.0f);
}
