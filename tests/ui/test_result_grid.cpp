#include "test_framework.h"

#include "ui/result_grid.h"

using ui::ResultView;
using ui::MoveDir;

TEST(result_grid_default_is_list)
{
    // The screen starts in List view (original Phase 18 behaviour).
    CHECK_TRUE(ResultView::List == ResultView{});
}

TEST(result_grid_toggle_cycles_two_modes)
{
    CHECK_TRUE(ui::toggle_result_view(ResultView::List) == ResultView::Grid);
    CHECK_TRUE(ui::toggle_result_view(ResultView::Grid) == ResultView::List);
    // Toggling twice returns to the original (persistence within a lifetime is
    // just the stored value; the toggle is its own inverse).
    CHECK_TRUE(ui::toggle_result_view(ui::toggle_result_view(ResultView::List))
               == ResultView::List);
}

TEST(result_grid_list_move_deltas)
{
    // List view: one result per row. Up/Down step ±1; Left/Right do nothing.
    CHECK_EQ(ui::result_move_delta(ResultView::List, MoveDir::Up, 4), -1);
    CHECK_EQ(ui::result_move_delta(ResultView::List, MoveDir::Down, 4), 1);
    CHECK_EQ(ui::result_move_delta(ResultView::List, MoveDir::Left, 4), 0);
    CHECK_EQ(ui::result_move_delta(ResultView::List, MoveDir::Right, 4), 0);
}

TEST(result_grid_grid_move_deltas)
{
    // Grid view: Left/Right step ±1; Up/Down step a whole row (±cols).
    CHECK_EQ(ui::result_move_delta(ResultView::Grid, MoveDir::Left, 4), -1);
    CHECK_EQ(ui::result_move_delta(ResultView::Grid, MoveDir::Right, 4), 1);
    CHECK_EQ(ui::result_move_delta(ResultView::Grid, MoveDir::Up, 4), -4);
    CHECK_EQ(ui::result_move_delta(ResultView::Grid, MoveDir::Down, 4), 4);
}

TEST(result_grid_grid_move_clamps_cols_to_one)
{
    // A degenerate column count must never produce a zero/negative row stride
    // (which would freeze Up/Down navigation).
    CHECK_EQ(ui::result_move_delta(ResultView::Grid, MoveDir::Down, 0), 1);
    CHECK_EQ(ui::result_move_delta(ResultView::Grid, MoveDir::Up, -3), -1);
}

TEST(result_grid_move_clamps_into_range)
{
    // result_move clamps the new index into [0, count).
    CHECK_EQ(ui::result_move(ResultView::List, 0, MoveDir::Up, 5, 1), 0);     // can't go above 0
    CHECK_EQ(ui::result_move(ResultView::List, 4, MoveDir::Down, 5, 1), 4);   // can't pass last
    CHECK_EQ(ui::result_move(ResultView::List, 2, MoveDir::Down, 5, 1), 3);

    // Grid: from index 1 with 4 cols, Down lands on 5 (clamped to last=6 here).
    CHECK_EQ(ui::result_move(ResultView::Grid, 1, MoveDir::Down, 7, 4), 5);
    CHECK_EQ(ui::result_move(ResultView::Grid, 5, MoveDir::Down, 7, 4), 6);   // clamp to last
    CHECK_EQ(ui::result_move(ResultView::Grid, 1, MoveDir::Right, 7, 4), 2);
    CHECK_EQ(ui::result_move(ResultView::Grid, 0, MoveDir::Left, 7, 4), 0);
}

TEST(result_grid_move_empty_is_zero)
{
    // No results: every move resolves to 0 (a safe, in-range cursor).
    CHECK_EQ(ui::result_move(ResultView::Grid, 0, MoveDir::Down, 0, 4), 0);
    CHECK_EQ(ui::result_move(ResultView::List, 3, MoveDir::Up, 0, 1), 0);
}
