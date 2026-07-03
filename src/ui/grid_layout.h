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

// Compute the minimum scroll adjustment needed to keep an item visible in the viewport.
// Inputs:
//   scroll: current scroll offset (pixels scrolled down)
//   item_top: top Y of the item (in document coordinates)
//   item_bottom: bottom Y of the item (in document coordinates)
//   view_top: top of the viewport (in document coordinates, typically = scroll)
//   view_bottom: bottom of the viewport (in document coordinates, typically = scroll + view_height)
// Returns the adjusted scroll value that keeps the item visible.
//   If item is taller than the view, aligns item_top with view_top.
//   If item is already visible, returns the input scroll unchanged.
[[nodiscard]] float ensure_visible(float scroll, float item_top, float item_bottom,
                                   float view_top, float view_bottom) noexcept;

// Clamp a scroll offset to the valid range [0, max(0, content_height - view_height)].
// Inputs:
//   scroll: the scroll offset to clamp
//   content_height: total height of scrollable content
//   view_height: height of the visible viewport
// Returns the clamped scroll value.
[[nodiscard]] float clamp_scroll(float scroll, float content_height,
                                 float view_height) noexcept;

}  // namespace ui
