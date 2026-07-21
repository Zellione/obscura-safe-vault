#include "test_framework.h"

#include "ui/detail_panel.h"

TEST(detail_panel_closed_reserves_nothing)
{
    CHECK_EQ(ui::detail_panel_width(false, 1920.0F), 0.0F);
}

TEST(detail_panel_open_reserves_fixed_width)
{
    CHECK_EQ(ui::detail_panel_width(true, 1920.0F), ui::DETAIL_PANEL_WIDTH);
}

TEST(detail_panel_hidden_on_narrow_window)
{
    CHECK_EQ(ui::detail_panel_width(true, 500.0F), 0.0F);
}

TEST(detail_panel_min_window_is_inclusive)
{
    // Exactly at the threshold the panel shows; one pixel under, it does not.
    CHECK_EQ(ui::detail_panel_width(true, ui::DETAIL_PANEL_MIN_WINDOW),
             ui::DETAIL_PANEL_WIDTH);
    CHECK_EQ(ui::detail_panel_width(true, ui::DETAIL_PANEL_MIN_WINDOW - 1.0F), 0.0F);
}

TEST(detail_panel_never_reserves_more_than_the_window)
{
    // Degenerate/zero-size windows must not produce a negative content width.
    CHECK_EQ(ui::detail_panel_width(true, 0.0F), 0.0F);
    CHECK(ui::detail_panel_width(true, 2000.0F) < 2000.0F);
}
