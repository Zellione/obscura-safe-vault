#include "ui/grid_layout.h"

#include <cmath>
#include <algorithm>

namespace ui {

std::pair<int, int> grid_visible_range(float scroll_offset, float cell_size,
                                        float gap, float origin_y,
                                        float viewport_height, int columns,
                                        int item_count) noexcept
{
    // Empty grid: no items to render.
    if (item_count <= 0 || columns <= 0)
        return {0, -1};

    const float pitch = cell_size + gap;
    const int total_rows = (item_count + columns - 1) / columns;

    const float viewport_top = scroll_offset;
    const float viewport_bottom = scroll_offset + viewport_height;

    // Find the first and last visible rows by checking which rows overlap with the
    // viewport. A row r spans [origin_y + r*pitch, origin_y + r*pitch + cell_size).
    // Two ranges overlap if: range1_start < range2_end AND range2_start < range1_end.

    // First row whose bottom is above the viewport top:
    // origin_y + r*pitch + cell_size > viewport_top
    // r > (viewport_top - origin_y - cell_size) / pitch
    int first_row = std::max(0, 1 + static_cast<int>(
        std::floor((viewport_top - origin_y - cell_size) / pitch)));

    // Last row whose top is below the viewport bottom:
    // origin_y + last_row * pitch < viewport_bottom
    // last_row < (viewport_bottom - origin_y) / pitch
    int last_row = std::min(total_rows - 1, static_cast<int>(
        std::floor((viewport_bottom - origin_y) / pitch - 0.001f)));

    // If no rows are visible, return empty range.
    if (last_row < first_row)
        return {0, -1};

    // Add one-row margin above and below.
    first_row = std::max(0, first_row - 1);
    last_row = std::min(total_rows - 1, last_row + 1);

    // Convert row range to index range.
    const int first_index = first_row * columns;
    const int end_index = std::min((last_row + 1) * columns - 1, item_count - 1);

    return {first_index, end_index};
}

std::pair<int, int> list_visible_range(float scroll_offset, float row_height,
                                        float header_offset, float viewport_height,
                                        int item_count) noexcept
{
    // Empty list: no items to render.
    if (item_count <= 0 || row_height <= 0.0f)
        return {0, -1};

    const float viewport_top = scroll_offset;
    const float viewport_bottom = scroll_offset + viewport_height;

    // First row whose bottom is above the viewport top.
    // Row i spans [header_offset + i*row_height, header_offset + (i+1)*row_height).
    // (header_offset + (first_row + 1) * row_height) > viewport_top
    // first_row > (viewport_top - header_offset) / row_height - 1
    int first_row = std::max(0, 1 + static_cast<int>(
        std::floor((viewport_top - header_offset) / row_height - 1.0f)));

    // Last row whose top is below the viewport bottom.
    // header_offset + last_row * row_height < viewport_bottom
    // last_row < (viewport_bottom - header_offset) / row_height
    int last_row = std::min(item_count - 1, static_cast<int>(
        std::floor((viewport_bottom - header_offset) / row_height - 0.001f)));

    // If no rows are visible, return empty range.
    if (last_row < first_row)
        return {0, -1};

    // Add one-row margin above and below.
    first_row = std::max(0, first_row - 1);
    last_row = std::min(item_count - 1, last_row + 1);

    return {first_row, last_row};
}

float ensure_visible(float scroll, float item_top, float item_bottom,
                     float view_top, float view_bottom) noexcept
{
    const float item_height = item_bottom - item_top;
    const float view_height = view_bottom - view_top;

    // If the item is taller than the view, align its top with the view top.
    if (item_height > view_height) {
        return item_top - view_top;
    }

    // Compute the item's position in viewport coordinates using the formula:
    // viewport_y = document_y - scroll
    const float item_view_top = item_top - scroll;
    const float item_view_bottom = item_bottom - scroll;

    // Check if item is already visible within [view_top, view_bottom].
    if (item_view_top >= view_top && item_view_bottom <= view_bottom) {
        return scroll;  // Item already visible
    }

    // If item is above the view (item_view_bottom < view_top),
    // scroll up (reduce scroll) to align item_top with view_top.
    if (item_view_bottom < view_top) {
        return item_top - view_top;
    }

    // If item is below the view (item_view_top > view_bottom),
    // scroll down (increase scroll) to align item_bottom with view_bottom.
    if (item_view_top > view_bottom) {
        return item_bottom - view_bottom;
    }

    return scroll;
}

float clamp_scroll(float scroll, float content_height, float view_height) noexcept
{
    const float max_scroll = std::max(0.0f, content_height - view_height);
    return std::max(0.0f, std::min(scroll, max_scroll));
}

}  // namespace ui
