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

TEST(strip_cell_rect_horizontal_first_cell)
{
    // Horizontal (Bottom) strip: thumb 30, gap 10, origin at x=100.
    const SDL_FRect strip{100.0f, 500.0f, 500.0f, 50.0f};
    const SDL_FRect cell = ui::strip_cell_rect(0, strip, 30.0f, 10.0f, 0.0f, false);

    // First cell starts at origin 100, size 30x30.
    // Vertically centred on the strip's cross-axis (y = 500 + (50 - 30) * 0.5 = 510).
    CHECK_EQ(cell.x, 100.0f);
    CHECK_EQ(cell.y, 510.0f);
    CHECK_EQ(cell.w, 30.0f);
    CHECK_EQ(cell.h, 30.0f);
}

TEST(strip_cell_rect_horizontal_interior_cell)
{
    // Interior cell (index 2) in a horizontal strip.
    const SDL_FRect strip{100.0f, 500.0f, 500.0f, 50.0f};
    const SDL_FRect cell = ui::strip_cell_rect(2, strip, 30.0f, 10.0f, 0.0f, false);

    // Cell 2 is at content offset 2 * (30 + 10) = 80, so screen x = 100 + 80 = 180.
    CHECK_EQ(cell.x, 180.0f);
    CHECK_EQ(cell.y, 510.0f);
    CHECK_EQ(cell.w, 30.0f);
    CHECK_EQ(cell.h, 30.0f);
}

TEST(strip_cell_rect_horizontal_with_scroll)
{
    // Horizontal strip with scroll 50: cells shift left on screen.
    const SDL_FRect strip{100.0f, 500.0f, 500.0f, 50.0f};
    const SDL_FRect cell = ui::strip_cell_rect(1, strip, 30.0f, 10.0f, 50.0f, false);

    // Cell 1 is at content offset 1 * (30 + 10) = 40.
    // With scroll 50, it lands at screen x = 100 - 50 + 40 = 90.
    CHECK_EQ(cell.x, 90.0f);
    CHECK_EQ(cell.y, 510.0f);
    CHECK_EQ(cell.w, 30.0f);
    CHECK_EQ(cell.h, 30.0f);
}

TEST(strip_cell_rect_vertical_first_cell)
{
    // Vertical (Left) strip: thumb 30, gap 10, origin at y=100.
    const SDL_FRect strip{0.0f, 100.0f, 50.0f, 500.0f};
    const SDL_FRect cell = ui::strip_cell_rect(0, strip, 30.0f, 10.0f, 0.0f, true);

    // First cell starts at origin 100, size 30x30.
    // Horizontally centred on the strip's cross-axis (x = 0 + (50 - 30) * 0.5 = 10).
    CHECK_EQ(cell.x, 10.0f);
    CHECK_EQ(cell.y, 100.0f);
    CHECK_EQ(cell.w, 30.0f);
    CHECK_EQ(cell.h, 30.0f);
}

TEST(strip_cell_rect_vertical_interior_cell)
{
    // Interior cell (index 2) in a vertical strip.
    const SDL_FRect strip{0.0f, 100.0f, 50.0f, 500.0f};
    const SDL_FRect cell = ui::strip_cell_rect(2, strip, 30.0f, 10.0f, 0.0f, true);

    // Cell 2 is at content offset 2 * (30 + 10) = 80, so screen y = 100 + 80 = 180.
    CHECK_EQ(cell.x, 10.0f);
    CHECK_EQ(cell.y, 180.0f);
    CHECK_EQ(cell.w, 30.0f);
    CHECK_EQ(cell.h, 30.0f);
}

TEST(strip_cell_rect_vertical_with_scroll)
{
    // Vertical strip with scroll 50: cells shift up on screen.
    const SDL_FRect strip{0.0f, 100.0f, 50.0f, 500.0f};
    const SDL_FRect cell = ui::strip_cell_rect(1, strip, 30.0f, 10.0f, 50.0f, true);

    // Cell 1 is at content offset 1 * (30 + 10) = 40.
    // With scroll 50, it lands at screen y = 100 - 50 + 40 = 90.
    CHECK_EQ(cell.x, 10.0f);
    CHECK_EQ(cell.y, 90.0f);
    CHECK_EQ(cell.w, 30.0f);
    CHECK_EQ(cell.h, 30.0f);
}

TEST(strip_cell_rect_and_hit_axis_are_consistent)
{
    // Forward and inverse mappings must be consistent.
    // If strip_cell_rect places cell N at position P, then
    // strip_hit_axis should return N when queried at P.

    const SDL_FRect strip{50.0f, 100.0f, 600.0f, 400.0f};
    const float thumb = 40.0f;
    const float gap = 12.0f;
    const float scroll = 30.0f;

    // Test horizontal strip.
    {
        const SDL_FRect cell = ui::strip_cell_rect(3, strip, thumb, gap, scroll, false);

        // Hit-test at the centre of the cell.
        const float along = cell.x + thumb * 0.5f;
        const float origin_along = strip.x;
        const int hit = ui::strip_hit_axis(along, origin_along, scroll, thumb, gap, 10);
        CHECK_EQ(hit, 3);
    }

    // Test vertical strip.
    {
        const SDL_FRect cell = ui::strip_cell_rect(3, strip, thumb, gap, scroll, true);

        // Hit-test at the centre of the cell.
        const float along = cell.y + thumb * 0.5f;
        const float origin_along = strip.y;
        const int hit = ui::strip_hit_axis(along, origin_along, scroll, thumb, gap, 10);
        CHECK_EQ(hit, 3);
    }
}

TEST(strip_cell_rect_gap_between_cells)
{
    // Consecutive cells must maintain the gap between them.
    const SDL_FRect strip{0.0f, 0.0f, 500.0f, 100.0f};
    const float thumb = 30.0f;
    const float gap = 10.0f;

    const SDL_FRect cell0 = ui::strip_cell_rect(0, strip, thumb, gap, 0.0f, false);
    const SDL_FRect cell1 = ui::strip_cell_rect(1, strip, thumb, gap, 0.0f, false);

    // Cell 0 ends at x=30, cell 1 starts at x=40. Gap is 10.
    CHECK_EQ(cell0.x, 0.0f);
    CHECK_EQ(cell0.w, 30.0f);
    CHECK_EQ(cell1.x, 40.0f);
    CHECK_EQ(cell1.x - (cell0.x + cell0.w), gap);
}
