#include "ui/result_grid.h"

#include <algorithm>

namespace ui {

ResultView toggle_result_view(ResultView v) noexcept
{
    return v == ResultView::List ? ResultView::Grid : ResultView::List;
}

int result_move_delta(ResultView v, MoveDir dir, int cols) noexcept
{
    const int row = std::max(1, cols);   // never let a bad layout zero the stride
    switch (dir) {
        case MoveDir::Left:  return v == ResultView::Grid ? -1 : 0;
        case MoveDir::Right: return v == ResultView::Grid ? 1 : 0;
        case MoveDir::Up:    return v == ResultView::Grid ? -row : -1;
        case MoveDir::Down:  return v == ResultView::Grid ? row : 1;
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
