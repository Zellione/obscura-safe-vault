#pragma once

// Pure geometry for the F1 help popup (Phase 51). Scroll is measured in WHOLE
// LINES, not pixels, and the visible band is a whole number of lines — together
// these make a partially-clipped line at either edge unrepresentable rather than
// merely unlikely. No SDL, no FontAtlas.

#include <cstddef>
#include <vector>

namespace ui {

struct HelpGroup;   // ui/help_popup.h

// How many WHOLE lines of height `line_h` fit in `viewport_h`. Floors — a line
// that only partly fits is not visible. Never negative.
[[nodiscard]] int help_visible_lines(float viewport_h, float line_h);

// Clamp a line-index scroll offset into [0, max(0, total_lines - visible_lines)].
[[nodiscard]] int clamp_help_line(int scroll_line, int total_lines, int visible_lines);

// One packed column: which groups it holds (indices into the caller's group
// vector, in order) and how many rendered lines they occupy.
struct HelpColumn {
    std::vector<size_t> group_indices;
    int                 lines = 0;
};

// How many columns a panel `panel_w` px wide should use. Two above the
// threshold, one below — a two-column layout in a narrow panel elides every
// description into uselessness.
[[nodiscard]] int help_column_count(float panel_w);

// Assign whole groups to at most `max_columns` columns of `lines_per_column`
// lines each, in order. A group is NEVER split across a column boundary: if it
// does not fit in the current column and another column is available, it starts
// the next one. A group taller than a whole column is emitted anyway (it
// scrolls) rather than dropped. With max_columns == 1 every group lands in the
// single column and the caller scrolls it.
[[nodiscard]] std::vector<HelpColumn> pack_help_columns(const std::vector<HelpGroup>& groups,
                                                        int lines_per_column,
                                                        int max_columns);

} // namespace ui
