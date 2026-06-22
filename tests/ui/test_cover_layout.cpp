#include "test_framework.h"

#include <SDL3/SDL.h>

#include "ui/cover_layout.h"

namespace {
constexpr SDL_FRect BOX{10.0f, 20.0f, 100.0f, 80.0f};
constexpr float     GAP = 4.0f;
}  // namespace

TEST(cover_layout_single_fills_box)
{
    const auto rects = ui::cover_montage_rects(BOX, 1, GAP);
    CHECK_EQ(static_cast<int>(rects.size()), 1);
    CHECK_EQ(rects[0].x, BOX.x);
    CHECK_EQ(rects[0].y, BOX.y);
    CHECK_EQ(rects[0].w, BOX.w);
    CHECK_EQ(rects[0].h, BOX.h);
}

TEST(cover_layout_four_is_2x2_grid)
{
    const auto rects = ui::cover_montage_rects(BOX, 4, GAP);
    CHECK_EQ(static_cast<int>(rects.size()), 4);

    const float cw = (BOX.w - GAP) * 0.5f;  // 48
    const float ch = (BOX.h - GAP) * 0.5f;  // 38

    // TL
    CHECK_EQ(rects[0].x, BOX.x);
    CHECK_EQ(rects[0].y, BOX.y);
    CHECK_EQ(rects[0].w, cw);
    CHECK_EQ(rects[0].h, ch);
    // TR
    CHECK_EQ(rects[1].x, BOX.x + cw + GAP);
    CHECK_EQ(rects[1].y, BOX.y);
    // BL
    CHECK_EQ(rects[2].x, BOX.x);
    CHECK_EQ(rects[2].y, BOX.y + ch + GAP);
    // BR
    CHECK_EQ(rects[3].x, BOX.x + cw + GAP);
    CHECK_EQ(rects[3].y, BOX.y + ch + GAP);
    CHECK_EQ(rects[3].w, cw);
    CHECK_EQ(rects[3].h, ch);
}

TEST(cover_layout_three_uses_first_three_grid_cells)
{
    const auto rects = ui::cover_montage_rects(BOX, 3, GAP);
    CHECK_EQ(static_cast<int>(rects.size()), 3);

    const float cw = (BOX.w - GAP) * 0.5f;
    const float ch = (BOX.h - GAP) * 0.5f;
    // TL, TR, BL — bottom-right left blank.
    CHECK_EQ(rects[0].x, BOX.x);
    CHECK_EQ(rects[0].y, BOX.y);
    CHECK_EQ(rects[1].x, BOX.x + cw + GAP);
    CHECK_EQ(rects[1].y, BOX.y);
    CHECK_EQ(rects[2].x, BOX.x);
    CHECK_EQ(rects[2].y, BOX.y + ch + GAP);
}

TEST(cover_layout_two_uses_top_row_of_grid)
{
    const auto rects = ui::cover_montage_rects(BOX, 2, GAP);
    CHECK_EQ(static_cast<int>(rects.size()), 2);

    const float cw = (BOX.w - GAP) * 0.5f;
    const float ch = (BOX.h - GAP) * 0.5f;
    CHECK_EQ(rects[0].x, BOX.x);
    CHECK_EQ(rects[0].y, BOX.y);
    CHECK_EQ(rects[0].w, cw);
    CHECK_EQ(rects[0].h, ch);
    CHECK_EQ(rects[1].x, BOX.x + cw + GAP);
    CHECK_EQ(rects[1].y, BOX.y);
}

TEST(cover_layout_zero_is_empty)
{
    CHECK(ui::cover_montage_rects(BOX, 0, GAP).empty());
    CHECK(ui::cover_montage_rects(BOX, -3, GAP).empty());
}

TEST(cover_layout_clamps_above_four)
{
    CHECK_EQ(static_cast<int>(ui::cover_montage_rects(BOX, 9, GAP).size()), 4);
}
