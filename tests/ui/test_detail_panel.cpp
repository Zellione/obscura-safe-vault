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

TEST(detail_panel_hit_false_when_closed)
{
    CHECK_FALSE(ui::detail_panel_hit(false, 1920.0f, 1900.0f));
}

TEST(detail_panel_hit_true_inside_the_strip)
{
    // Panel occupies [1920-280, 1920) = [1640, 1920).
    CHECK(ui::detail_panel_hit(true, 1920.0f, 1700.0f));
    CHECK(ui::detail_panel_hit(true, 1920.0f, 1919.0f));
}

TEST(detail_panel_hit_false_left_of_the_strip)
{
    CHECK_FALSE(ui::detail_panel_hit(true, 1920.0f, 1639.0f));
    CHECK_FALSE(ui::detail_panel_hit(true, 1920.0f, 0.0f));
}

TEST(detail_panel_hit_boundary_is_the_panel_edge)
{
    // Exactly on the left edge counts as inside the panel.
    CHECK(ui::detail_panel_hit(true, 1920.0f, 1920.0f - ui::DETAIL_PANEL_WIDTH));
}

TEST(detail_panel_hit_false_on_narrow_window)
{
    // Below the min window the panel reserves nothing, so nothing can hit it.
    CHECK_FALSE(ui::detail_panel_hit(true, 500.0f, 490.0f));
}

TEST(detail_panel_scroll_wheel_up_moves_toward_the_start)
{
    ui::DetailPanelState st{.open = true, .scroll = 200.0f};
    ui::scroll_detail_panel(st, 1.0f);          // wheel away from user
    CHECK(st.scroll < 200.0f);
}

TEST(detail_panel_scroll_wheel_down_moves_further_in)
{
    ui::DetailPanelState st{.open = true, .scroll = 0.0f};
    ui::scroll_detail_panel(st, -1.0f);         // wheel toward user
    CHECK(st.scroll > 0.0f);
}

TEST(detail_panel_scroll_clamps_at_zero)
{
    ui::DetailPanelState st{.open = true, .scroll = 5.0f};
    ui::scroll_detail_panel(st, 100.0f);        // huge upward flick
    CHECK_EQ(st.scroll, 0.0f);
}
