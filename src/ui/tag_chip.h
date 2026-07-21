#pragma once

#include <span>
#include <string>

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

// Draw one line of chips left-to-right starting at (x, y), clipped to `max_w`,
// with any remainder collapsed into a dimmed "+N". `tags` are RAW stored tags
// ("artist:Kaguya"); resolution against `categories` happens inside.
void draw_tag_chips(gfx::Renderer& r, gfx::FontAtlas& font, float x, float y, float max_w,
                    std::span<const std::string> tags,
                    std::span<const vault::TagCategory> categories);

// Pixel width one chip occupies, for callers that need to pre-measure a run.
[[nodiscard]] int chip_width(const gfx::FontAtlas& font, std::string_view display_text);

} // namespace ui
