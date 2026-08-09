#pragma once

// Phase 56: the one place a vertical list of text rows works out where its rows
// go. Five surfaces used to each carry their own copy of this arithmetic, and
// two of them advanced by LESS than the font's glyph height — the overlap this
// phase removes. `row_h` comes from ui::line_pitch / ui::row_height, never from
// a literal, so the pitch cannot drift below the font again.

namespace ui {

struct ListMetrics {
    float top   = 0.0f;   // y of the first row's top
    float row_h = 0.0f;   // height of one row (>= the font's glyph box)
    float gap   = 0.0f;   // extra space between rows (0 for a flush list)
};

// Top y of row `index` (0-based). Indices below 0 clamp to the first row.
[[nodiscard]] float list_row_y(const ListMetrics& m, int index);

// How many whole rows fit between `m.top` and `bottom`. A row that would be
// clipped is not counted. Never negative; 0 when the row height is degenerate.
[[nodiscard]] int list_visible_rows(const ListMetrics& m, float bottom);

// list_visible_rows capped by how many items there actually are.
[[nodiscard]] int list_rows_fit(const ListMetrics& m, float bottom, int count);

// Clamp scroll offset for a vertical list: ensures items are within [0, max_offset]
// where max_offset = max(0, content_height - available_height). Phase 68 Part 3.
// content_height = (count + 1) * row_h (header + row items)
// available_height = max_h - top (viewport height for scrollable area)
[[nodiscard]] float list_clamp_scroll(float scroll, int count, float row_h, float top,
                                      float max_h) noexcept;

} // namespace ui
