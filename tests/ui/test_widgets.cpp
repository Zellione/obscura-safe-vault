#include "test_framework.h"

#include <SDL3/SDL.h>

#include "ui/widgets.h"

TEST(widgets_point_in_rect)
{
    SDL_FRect r{10, 10, 100, 50};
    CHECK(ui::point_in_rect(10, 10, r));
    CHECK(ui::point_in_rect(60, 30, r));
    CHECK_FALSE(ui::point_in_rect(110, 30, r));   // right edge exclusive
    CHECK_FALSE(ui::point_in_rect(5, 30, r));
}

TEST(widgets_grid_columns_and_cell)
{
    CHECK_EQ(ui::grid_columns(680.0f, 160.0f, 16.0f), 3); // floor((680+16)/176)=3
    CHECK(ui::grid_columns(0.0f, 160.0f, 16.0f) >= 1);    // never zero

    SDL_FRect c = ui::grid_cell_rect(/*index*/5, /*cols*/4, /*cell*/160, /*gap*/16,
                                     /*ox*/40, /*oy*/120);
    // index 5 -> row 1, col 1
    CHECK_EQ(c.x, 40.0f + 1 * (160.0f + 16.0f));
    CHECK_EQ(c.y, 120.0f + 1 * (160.0f + 16.0f));
    CHECK_EQ(c.w, 160.0f);
}

TEST(widgets_grid_hit)
{
    // 4 cells, cols 4, cell 160 gap 16, origin (40,120)
    int hit = ui::grid_hit(40 + 1 * 176 + 5, 120 + 5, /*count*/4, /*cols*/4,
                           160, 16, 40, 120);
    CHECK_EQ(hit, 1);
    CHECK_EQ(ui::grid_hit(0, 0, 4, 4, 160, 16, 40, 120), -1);     // miss
}

TEST(widgets_fit_rect_preserves_aspect)
{
    SDL_FRect box{0, 0, 100, 100};
    SDL_FRect f = ui::fit_rect(200, 100, box);   // 2:1 image into square box
    CHECK_EQ(f.w, 100.0f);
    CHECK_EQ(f.h, 50.0f);
    CHECK_EQ(f.x, 0.0f);
    CHECK_EQ(f.y, 25.0f);                          // vertically centred
}
