#include "test_framework.h"

#include "ui/result_grid.h"

using ui::GalleryView;
using ui::MoveDir;

// The result panel's cursor movement is GalleryView-driven (Phase 93): List is
// one-per-row (the Phase 18 behaviour); any grid density moves Left/Right by
// one and Up/Down by a whole row.

TEST(result_grid_list_move_deltas)
{
    CHECK_EQ(ui::result_move_delta(GalleryView::List, MoveDir::Up, 4), -1);
    CHECK_EQ(ui::result_move_delta(GalleryView::List, MoveDir::Down, 4), 1);
    CHECK_EQ(ui::result_move_delta(GalleryView::List, MoveDir::Left, 4), 0);
    CHECK_EQ(ui::result_move_delta(GalleryView::List, MoveDir::Right, 4), 0);
}

TEST(result_grid_grid_move_deltas)
{
    // Every grid density behaves identically (cols drive the vertical stride).
    for (auto v : {GalleryView::GridS, GalleryView::GridM, GalleryView::GridL, GalleryView::GridXL,
                   GalleryView::GridXXL}) {
        CHECK_EQ(ui::result_move_delta(v, MoveDir::Left, 4), -1);
        CHECK_EQ(ui::result_move_delta(v, MoveDir::Right, 4), 1);
        CHECK_EQ(ui::result_move_delta(v, MoveDir::Up, 4), -4);
        CHECK_EQ(ui::result_move_delta(v, MoveDir::Down, 4), 4);
    }
}

TEST(result_grid_grid_move_clamps_cols_to_one)
{
    // A degenerate layout (cols <= 0) must not freeze navigation.
    CHECK_EQ(ui::result_move_delta(GalleryView::GridM, MoveDir::Down, 0), 1);
    CHECK_EQ(ui::result_move_delta(GalleryView::GridM, MoveDir::Up, -3), -1);
}

TEST(result_grid_move_clamps_into_range)
{
    // result_move clamps the new index into [0, count).
    CHECK_EQ(ui::result_move(GalleryView::List, 0, MoveDir::Up, 5, 1), 0);    // can't go above 0
    CHECK_EQ(ui::result_move(GalleryView::List, 4, MoveDir::Down, 5, 1), 4);  // can't pass last
    CHECK_EQ(ui::result_move(GalleryView::List, 2, MoveDir::Down, 5, 1), 3);

    CHECK_EQ(ui::result_move(GalleryView::GridXL, 1, MoveDir::Down, 7, 4), 5);
    CHECK_EQ(ui::result_move(GalleryView::GridXL, 5, MoveDir::Down, 7, 4), 6);  // clamp to last
    CHECK_EQ(ui::result_move(GalleryView::GridXL, 1, MoveDir::Right, 7, 4), 2);
    CHECK_EQ(ui::result_move(GalleryView::GridXL, 0, MoveDir::Left, 7, 4), 0);
}

TEST(result_grid_move_empty_is_zero)
{
    CHECK_EQ(ui::result_move(GalleryView::GridXXL, 0, MoveDir::Down, 0, 4), 0);
    CHECK_EQ(ui::result_move(GalleryView::List, 3, MoveDir::Up, 0, 1), 0);
}