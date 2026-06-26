#include "test_framework.h"

#include "ui/tag_scroll.h"

using ui::tag_scroll_first;

TEST(tag_scroll_all_fit_returns_zero)
{
    CHECK_EQ(tag_scroll_first(/*total*/ 3, /*selected*/ 2, /*max_visible*/ 5), 0);
    CHECK_EQ(tag_scroll_first(5, 4, 5), 0);   // exactly fits
}

TEST(tag_scroll_selection_within_first_window_stays_top)
{
    CHECK_EQ(tag_scroll_first(10, 0, 5), 0);
    CHECK_EQ(tag_scroll_first(10, 4, 5), 0);   // last row of the first window
}

TEST(tag_scroll_follows_selection_past_window)
{
    CHECK_EQ(tag_scroll_first(10, 5, 5), 1);   // selection at bottom of window
    CHECK_EQ(tag_scroll_first(10, 6, 5), 2);
}

TEST(tag_scroll_clamps_at_bottom)
{
    CHECK_EQ(tag_scroll_first(10, 9, 5), 5);   // show last 5 rows (5..9)
}

TEST(tag_scroll_handles_out_of_range_and_degenerate)
{
    CHECK_EQ(tag_scroll_first(10, 100, 5), 5);  // selection past the end -> last window
    CHECK_EQ(tag_scroll_first(10, -3, 5), 0);   // negative selection -> top
    CHECK_EQ(tag_scroll_first(0, 0, 5), 0);     // empty list
    CHECK_EQ(tag_scroll_first(10, 4, 0), 0);    // no visible rows -> top (no div by zero)
}
