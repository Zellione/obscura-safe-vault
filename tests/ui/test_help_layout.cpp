// Phase 51: pure help-popup geometry. Scroll is tracked in WHOLE LINES and the
// band is sized to a whole number of lines, so a half-drawn line at either edge
// is structurally unrepresentable rather than merely avoided.

#include "test_framework.h"

#include <string>
#include <vector>

#include "ui/help_layout.h"
#include "ui/help_popup.h"

TEST(help_visible_lines_floors_to_whole_lines)
{
    CHECK_EQ(ui::help_visible_lines(408.0f, 24.0f), 17);
    CHECK_EQ(ui::help_visible_lines(407.9f, 24.0f), 16);   // NOT 17 — a partial line is not visible
    CHECK_EQ(ui::help_visible_lines(388.0f, 24.0f), 16);   // the sub-600px-window case
}

TEST(help_visible_lines_is_never_negative)
{
    CHECK_EQ(ui::help_visible_lines(0.0f, 24.0f), 0);
    CHECK_EQ(ui::help_visible_lines(-50.0f, 24.0f), 0);
    CHECK_EQ(ui::help_visible_lines(100.0f, 0.0f), 0);
}

TEST(help_clamp_line_bounds_to_the_last_full_page)
{
    CHECK_EQ(ui::clamp_help_line(0, 40, 17), 0);
    CHECK_EQ(ui::clamp_help_line(23, 40, 17), 23);
    CHECK_EQ(ui::clamp_help_line(99, 40, 17), 23);    // 40 - 17
    CHECK_EQ(ui::clamp_help_line(-5, 40, 17), 0);
}

TEST(help_clamp_line_is_zero_when_everything_fits)
{
    CHECK_EQ(ui::clamp_help_line(5, 10, 17), 0);
    CHECK_EQ(ui::clamp_help_line(5, 17, 17), 0);
}

TEST(help_every_line_is_reachable_at_max_scroll)
{
    // The regression guard for the reported bug: at maximum scroll the LAST
    // content line must be the last line drawn, and it must sit fully inside the
    // band — i.e. its index is exactly visible_lines-1 rows below the top line.
    const int total = 40;
    const int visible = 17;
    const int max_scroll = ui::clamp_help_line(9999, total, visible);
    CHECK_EQ(max_scroll + visible, total);
}

namespace {

ui::HelpGroup group_of(const char* title, int entries)
{
    ui::HelpGroup g;
    g.title = title;
    for (int i = 0; i < entries; ++i) {
        g.entries.push_back({.key = "K", .description = "d"});
    }
    return g;
}

} // namespace

TEST(help_column_count_is_one_when_narrow)
{
    CHECK_EQ(ui::help_column_count(400.0f), 1);
}

TEST(help_column_count_is_two_when_wide)
{
    CHECK_EQ(ui::help_column_count(680.0f), 2);
}

TEST(help_pack_columns_never_splits_a_group)
{
    // Two 5-line groups (title + 4 entries) into columns of 6 lines: each group
    // must land whole in its own column rather than straddling the boundary.
    const std::vector<ui::HelpGroup> g{group_of("A", 4), group_of("B", 4)};
    const auto cols = ui::pack_help_columns(g, 6, 2);
    CHECK_EQ(cols.size(), 2u);
    CHECK_EQ(cols[0].group_indices.size(), 1u);
    CHECK_EQ(cols[0].group_indices[0], 0u);
    CHECK_EQ(cols[1].group_indices.size(), 1u);
    CHECK_EQ(cols[1].group_indices[0], 1u);
}

TEST(help_pack_columns_fills_a_column_before_moving_on)
{
    const std::vector<ui::HelpGroup> g{group_of("A", 1), group_of("B", 1), group_of("C", 1)};
    const auto cols = ui::pack_help_columns(g, 10, 2);
    CHECK_EQ(cols.size(), 1u);                    // all three fit in one column
    CHECK_EQ(cols[0].group_indices.size(), 3u);
}

TEST(help_pack_columns_keeps_an_oversized_group_visible)
{
    // A group taller than a whole column must still be emitted (it will scroll)
    // rather than being dropped — otherwise content vanishes silently.
    const std::vector<ui::HelpGroup> g{group_of("Huge", 50)};
    const auto cols = ui::pack_help_columns(g, 6, 2);
    CHECK_EQ(cols.size(), 1u);
    CHECK_EQ(cols[0].group_indices.size(), 1u);
}

TEST(help_pack_columns_single_column_returns_every_group)
{
    const std::vector<ui::HelpGroup> g{group_of("A", 2), group_of("B", 2), group_of("C", 2)};
    const auto cols = ui::pack_help_columns(g, 3, 1);
    CHECK_EQ(cols.size(), 1u);
    CHECK_EQ(cols[0].group_indices.size(), 3u);   // one column holds everything, scrolled
}

TEST(help_pack_columns_empty_input_is_empty)
{
    CHECK(ui::pack_help_columns({}, 10, 2).empty());
}
