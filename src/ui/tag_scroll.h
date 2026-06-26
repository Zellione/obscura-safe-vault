#pragma once

#include <algorithm>

namespace ui {

// First visible row index for a scrolling list of `total` rows that can show at
// most `max_visible` at once, scrolled just enough to keep `selected` in view.
// Stateless and clamped to [0, total - max_visible]; returns 0 when everything
// fits or the inputs are degenerate. Pure (Phase 21 tag-editor scroll fix).
[[nodiscard]] inline int tag_scroll_first(int total, int selected, int max_visible)
{
    if (max_visible <= 0 || total <= max_visible) return 0;
    selected = std::clamp(selected, 0, total - 1);
    const int first = selected >= max_visible ? selected - max_visible + 1 : 0;
    return std::clamp(first, 0, total - max_visible);
}

}  // namespace ui
