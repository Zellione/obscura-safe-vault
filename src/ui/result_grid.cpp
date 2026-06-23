#include "ui/result_grid.h"

#include <algorithm>

namespace ui {

ResultView toggle_result_view(ResultView v) noexcept
{
    using enum ResultView;
    return v == List ? Grid : List;
}

int result_move_delta(ResultView v, MoveDir dir, int cols) noexcept
{
    using enum MoveDir;
    const bool grid = (v == ResultView::Grid);
    const int  row  = std::max(1, cols);   // never let a bad layout zero the stride
    switch (dir) {
        case Left:  return grid ? -1 : 0;
        case Right: return grid ? 1 : 0;
        case Up:    return grid ? -row : -1;
        case Down:  return grid ? row : 1;
    }
    return 0;
}

int result_move(ResultView v, int index, MoveDir dir, int count, int cols) noexcept
{
    if (count <= 0) return 0;
    const int next = index + result_move_delta(v, dir, cols);
    return std::clamp(next, 0, count - 1);
}

} // namespace ui
