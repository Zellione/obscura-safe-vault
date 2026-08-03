#pragma once

// Pure geometry for the duplicates-review screen: one group row is a header
// line, a horizontal band of member tiles, and three metadata text lines per
// tile. Tiles share the full content width — larger with 2 members, scaled
// down as the group grows — and every vertical extent derives from the font
// via ui::line_pitch, so rows can never draw past their advance.

#include <cstddef>

namespace ui {

inline constexpr float DUP_TILE_GAP   = 12.0f;   // horizontal gap between tiles
inline constexpr float DUP_GROUP_GAP  = 24.0f;   // vertical gap between group rows
inline constexpr float DUP_MIN_TILE_W = 100.0f;  // readability floor per tile

struct DupRowLayout {
    float tile_w   = 0.0f;  // one member tile's width
    float tile_h   = 0.0f;  // tile (image area) height
    float first_x  = 0.0f;  // x of the leftmost tile (row centered in the band)
    float header_h = 0.0f;  // group header line + gap above the tiles
    float text_h   = 0.0f;  // metadata block below the tiles (3 lines + pad)
    float row_h    = 0.0f;  // full row advance incl. DUP_GROUP_GAP
};

// content_x/content_w: the usable horizontal band. tile_h: desired image
// height (from dup_tile_height). font_px: the UI font size driving line pitch.
[[nodiscard]] DupRowLayout dup_row_layout(float content_x, float content_w,
                                          float tile_h, float font_px,
                                          std::size_t member_count) noexcept;

// Tile height for a given window height: half the space between the list top
// and the footer, clamped to [180, 440].
[[nodiscard]] float dup_tile_height(float window_h) noexcept;

// Opaque footer band height: three font-derived lines (status, marked totals,
// key hints) plus padding.
[[nodiscard]] float dup_footer_height(float font_px) noexcept;

} // namespace ui
