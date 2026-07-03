#pragma once

#include <utility>

// Pure, SDL/vault-free layout utility for computing visible index ranges in grids
// and lists. Used for viewport culling to skip rendering off-screen items.
// No dependencies beyond standard C++; unit-testable.
namespace ui {

// Visible index range in a grid, with one-row margin above and below.
// Inputs:
//   scroll_offset: pixels scrolled (positive = scrolled down)
//   cell_size: width/height of square cells
//   gap: gap between cells
//   origin_y: top-left Y of the grid
//   viewport_height: visible height (typically window height)
//   columns: number of columns (>= 1)
//   item_count: total items in the grid
// Returns {first_index, end_index} (inclusive range, clamped to [0, item_count)).
//   Empty grid (item_count == 0) returns {0, -1}.
[[nodiscard]] std::pair<int, int> grid_visible_range(float scroll_offset, float cell_size,
                                                      float gap, float origin_y,
                                                      float viewport_height, int columns,
                                                      int item_count) noexcept;

// Visible index range in a list, with one-row margin above and below.
// Inputs:
//   scroll_offset: pixels scrolled (positive = scrolled down)
//   row_height: height of each row
//   header_offset: Y position of first row (typically after a header)
//   viewport_height: visible height (typically window height)
//   item_count: total items in the list
// Returns {first_index, end_index} (inclusive range, clamped to [0, item_count)).
//   Empty list (item_count == 0) returns {0, -1}.
[[nodiscard]] std::pair<int, int> list_visible_range(float scroll_offset, float row_height,
                                                      float header_offset, float viewport_height,
                                                      int item_count) noexcept;

}  // namespace ui
