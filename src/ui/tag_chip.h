#pragma once

#include <span>
#include <string>
#include <vector>

#include "gfx/color.h"
#include "ui/tag_category.h"

namespace gfx { class Renderer; class FontAtlas; }

namespace ui {

// Chip geometry (Phase 49). A chip is a filled dot plus the tag's display text —
// no pill: the densest form, closest to the plain-text list it replaces.
inline constexpr float CHIP_DOT     = 9.0f;    // dot diameter
inline constexpr float CHIP_GAP     = 7.0f;    // dot → text
inline constexpr float CHIP_SPACING = 12.0f;   // chip → chip
inline constexpr float CHIP_ROW_H   = 16.0f;   // height a chip line reserves

// How many chips of the given pixel widths fit in `avail_w`, and how many are
// folded into a "+N" counter of width `overflow_w`. A chip width is the whole
// chip (dot + gap + text). Pure; unit-tested.
struct ChipFit { int shown = 0; int hidden = 0; };
[[nodiscard]] ChipFit fit_chips(std::span<const int> chip_widths, float avail_w,
                                float overflow_w) noexcept;

// One line of a wrapped chip run: the half-open range [first, first + count) of
// the width array that sits on it, and the pixels those chips occupy.
struct ChipLine { int first = 0; int count = 0; float width = 0.0f; };

// Greedy-wrap chips into at most `max_lines` lines of `max_w`, using the same
// spacing rule as fit_chips. Only the LAST line reserves room for the "+N"
// counter, because only there can chips be left over. Chips that do not fit are
// reported in `hidden`; the caller draws the counter after the last line's
// `width`. Pure; unit-tested.
struct ChipWrap { std::vector<ChipLine> lines; int hidden = 0; };
[[nodiscard]] ChipWrap pack_chip_lines(std::span<const int> chip_widths, float max_w,
                                       int max_lines, float overflow_w);

// Draw one line of chips left-to-right starting at (x, y), clipped to `max_w`,
// with any remainder collapsed into a dimmed "+N". `tags` are RAW stored tags
// ("artist:Kaguya"); resolution against `categories` happens inside.
void draw_tag_chips(gfx::Renderer& r, gfx::FontAtlas& font, float x, float y, float max_w,
                    std::span<const std::string> tags,
                    std::span<const vault::TagCategory> categories);

// Pixel width one chip occupies, for callers that need to pre-measure a run.
[[nodiscard]] int chip_width(const gfx::FontAtlas& font, std::string_view display_text);

// Width left for a single chip's TEXT when not even one whole chip fits in
// `max_w`. Subtracts the dot and the gap, plus the "+N" counter and its spacing
// when `hidden_after` further tags follow. May be <= 0, meaning not even an
// ellipsis will fit. Pure; unit-tested.
[[nodiscard]] float lone_chip_text_w(float max_w, float overflow_w, int hidden_after) noexcept;

} // namespace ui
