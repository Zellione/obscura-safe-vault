#include "test_framework.h"

#include <SDL3/SDL.h>

#include "ui/strip_layout.h"

using ui::StripSide;

TEST(strip_thumb_is_half_of_bottom_strip)
{
    // Window 800 tall -> bottom strip 200 -> (200 - 2*16) usable -> halved = 84.
    CHECK_EQ(ui::strip_thumb_size(800.0f), 84.0f);
    // Never collapses below the 8px floor on tiny windows.
    CHECK_EQ(ui::strip_thumb_size(0.0f), 8.0f);
}

TEST(left_strip_width_is_thumb_plus_padding)
{
    CHECK_EQ(ui::left_strip_width(84.0f), 84.0f + 2.0f * ui::STRIP_PAD);
}

TEST(bottom_strip_bar_hugs_the_thumbnails)
{
    // The bar is just the thumb plus a little padding (no dead space); the image
    // viewport gets everything above it.
    const float bar = 84.0f + 2.0f * ui::STRIP_PAD;

    const SDL_FRect vp = ui::viewport_rect_for(StripSide::Bottom, 1000, 800, 84);
    CHECK_EQ(vp.x, 0.0f);
    CHECK_EQ(vp.y, 0.0f);
    CHECK_EQ(vp.w, 1000.0f);
    CHECK_EQ(vp.h, 800.0f - bar);

    const SDL_FRect st = ui::strip_rect_for(StripSide::Bottom, 1000, 800, 84);
    CHECK_EQ(st.x, 0.0f);
    CHECK_EQ(st.y, 800.0f - bar);
    CHECK_EQ(st.w, 1000.0f);
    CHECK_EQ(st.h, bar);
}

TEST(left_viewport_and_strip_split_the_window)
{
    const float lw = ui::left_strip_width(84);   // 116
    const SDL_FRect st = ui::strip_rect_for(StripSide::Left, 1000, 800, 84);
    CHECK_EQ(st.x, 0.0f);
    CHECK_EQ(st.y, 0.0f);
    CHECK_EQ(st.w, lw);
    CHECK_EQ(st.h, 800.0f);

    const SDL_FRect vp = ui::viewport_rect_for(StripSide::Left, 1000, 800, 84);
    CHECK_EQ(vp.x, lw);
    CHECK_EQ(vp.y, 0.0f);
    CHECK_EQ(vp.w, 1000.0f - lw);
    CHECK_EQ(vp.h, 800.0f);
}

TEST(strip_hit_axis_maps_position_to_cell)
{
    // thumb 30, gap 10, 5 cells, origin 0, no scroll.
    CHECK_EQ(ui::strip_hit_axis(5,   0, 0, 30, 10, 5), 0);   // inside cell 0
    CHECK_EQ(ui::strip_hit_axis(35,  0, 0, 30, 10, 5), -1);  // in the gap (30..40)
    CHECK_EQ(ui::strip_hit_axis(45,  0, 0, 30, 10, 5), 1);   // inside cell 1
    CHECK_EQ(ui::strip_hit_axis(999, 0, 0, 30, 10, 5), -1);  // past the end

    // With scroll 50, cell 1 (content-x 40) lands near the origin.
    CHECK_EQ(ui::strip_hit_axis(5, 0, 50, 30, 10, 5), 1);
}
