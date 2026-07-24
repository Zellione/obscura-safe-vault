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
