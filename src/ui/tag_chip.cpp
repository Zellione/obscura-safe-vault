#include "ui/tag_chip.h"

#include <SDL3/SDL.h>

#include <format>
#include <vector>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"

// The persisted swatch byte is bounded by vault::TAG_SWATCH_COUNT; the palette
// it indexes lives in gfx. This is the one place both are visible, so it is the
// place to assert they have not drifted apart.
static_assert(gfx::TAG_SWATCH_COUNT == static_cast<int>(vault::TAG_SWATCH_COUNT),
              "gfx tag palette size must match the persisted swatch bound");

namespace ui {

ChipFit fit_chips(std::span<const int> chip_widths, float avail_w, float overflow_w) noexcept
{
    const int n = static_cast<int>(chip_widths.size());
    if (n == 0) {
        return {.shown = 0, .hidden = 0};
    }

    float used = 0.0f;
    int   shown = 0;
    for (int i = 0; i < n; ++i) {
        const float advance = (i == 0 ? 0.0f : CHIP_SPACING) + static_cast<float>(chip_widths[i]);
        if (used + advance > avail_w) {
            break;
        }
        used += advance;
        ++shown;
    }
    if (shown == n) {
        return {.shown = shown, .hidden = 0};
    }

    // Room must also be found for the "+N" counter; drop shown chips until it
    // fits.
    while (shown > 0 && used + CHIP_SPACING + overflow_w > avail_w) {
        --shown;
        used -= static_cast<float>(chip_widths[shown]) + (shown == 0 ? 0.0f : CHIP_SPACING);
    }
    return {.shown = shown, .hidden = n - shown};
}

int chip_width(const gfx::FontAtlas& font, std::string_view display_text)
{
    return static_cast<int>(CHIP_DOT + CHIP_GAP) + font.measure(display_text);
}

void draw_tag_chips(gfx::Renderer& r, gfx::FontAtlas& font, float x, float y, float max_w,
                    std::span<const std::string> tags,
                    std::span<const vault::TagCategory> categories)
{
    if (tags.empty() || max_w <= 0.0f) {
        return;
    }

    std::vector<TagDisplay> shown_tags;
    std::vector<int>        widths;
    shown_tags.reserve(tags.size());
    widths.reserve(tags.size());
    for (const std::string& t : tags) {
        const TagDisplay d = resolve_tag(t, categories);
        shown_tags.push_back(d);
        widths.push_back(chip_width(font, d.text));
    }

    const std::string overflow_probe = std::format("+{}", tags.size());
    const auto        fit = fit_chips(widths, max_w, static_cast<float>(font.measure(overflow_probe)));

    // The dot and the text share one vertical centre line. text_top_for_center
    // uses the baked glyphs' real ink extents, so this stays aligned regardless
    // of the font's nominal pixel height — do not hand-compute a baseline.
    const float center_y = y + CHIP_ROW_H * 0.5f;
    const float text_y   = font.text_top_for_center(center_y);

    float cx = x;
    for (int i = 0; i < fit.shown; ++i) {
        const TagDisplay& d = shown_tags[static_cast<size_t>(i)];
        const gfx::Color  c = d.swatch < 0 ? gfx::theme::TEXT_DIM : gfx::tag_swatch(d.swatch);

        const SDL_FRect dot{.x = cx,
                            .y = center_y - CHIP_DOT * 0.5f,
                            .w = CHIP_DOT,
                            .h = CHIP_DOT};
        r.draw_round_rect(dot, CHIP_DOT * 0.5f, c);
        r.draw_text(font, cx + CHIP_DOT + CHIP_GAP, text_y, d.text, c);

        cx += static_cast<float>(widths[static_cast<size_t>(i)]) + CHIP_SPACING;
    }
    if (fit.hidden > 0) {
        r.draw_text(font, cx, text_y, std::format("+{}", fit.hidden), gfx::theme::TEXT_FAINT);
    }
}

} // namespace ui
