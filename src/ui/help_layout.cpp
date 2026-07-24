#include "ui/help_layout.h"

#include <algorithm>
#include <cmath>

namespace ui {

int help_visible_lines(float viewport_h, float line_h)
{
    if (line_h <= 0.0f || viewport_h <= 0.0f) {
        return 0;
    }
    const int lines = static_cast<int>(std::floor(viewport_h / line_h));
    return lines < 0 ? 0 : lines;
}

int clamp_help_line(int scroll_line, int total_lines, int visible_lines) // NOLINT(bugprone-easily-swappable-parameters)
{
    const int max_scroll = std::max(0, total_lines - visible_lines);
    return std::clamp(scroll_line, 0, max_scroll);
}

} // namespace ui
