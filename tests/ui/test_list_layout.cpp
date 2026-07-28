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
