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

} // namespace ui
