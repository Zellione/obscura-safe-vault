#include "ui/help_layout.h"

#include <algorithm>
#include <cmath>

#include "ui/help_popup.h"

namespace ui {

int help_visible_lines(float viewport_h, float line_h)
{
    if (line_h <= 0.0f || viewport_h <= 0.0f) {
        return 0;
    }
    const auto lines = static_cast<int>(std::floor(viewport_h / line_h));
    return lines < 0 ? 0 : lines;
}

int clamp_help_line(int scroll_line, int total_lines, int visible_lines) // NOLINT(bugprone-easily-swappable-parameters)
{
    const int max_scroll = std::max(0, total_lines - visible_lines);
    return std::clamp(scroll_line, 0, max_scroll);
}

namespace {
// Rendered height of one group: its title plus one line per entry. The blank
// spacer line between groups is a *separator*, so it is charged only when the
// group is not first in its column.
int group_lines(const HelpGroup& g)
{
    return 1 + static_cast<int>(g.entries.size());
}
} // namespace

int help_column_count(float panel_w)
{
    // 640 px is the same "is there room for two things" threshold the detail
    // panel uses to decide whether it may open at all.
    return panel_w >= 640.0f ? 2 : 1;
}

std::vector<HelpColumn> pack_help_columns(const std::vector<HelpGroup>& groups,
                                          int lines_per_column, int max_columns) // NOLINT(bugprone-easily-swappable-parameters)
{
    std::vector<HelpColumn> cols;
    if (groups.empty()) {
        return cols;
    }

    const int columns = std::max(1, max_columns);
    cols.emplace_back();

    for (size_t i = 0; i < groups.size(); ++i) {
        const bool first_in_col = cols.back().group_indices.empty();
        const int  cost = group_lines(groups[i]) + (first_in_col ? 0 : 1);

        const bool overflows   = !first_in_col &&
                                 cols.back().lines + cost > lines_per_column;
        if (const bool can_advance = static_cast<int>(cols.size()) < columns; overflows && can_advance) {
            cols.emplace_back();
            cols.back().group_indices.push_back(i);
            cols.back().lines = group_lines(groups[i]);
            continue;
        }
        cols.back().group_indices.push_back(i);
        cols.back().lines += cost;
    }
    return cols;
}

} // namespace ui
