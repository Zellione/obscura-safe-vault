#include "ui/tag_chip.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>
#include <utility>
#include <vector>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "ui/widgets.h"

// The persisted swatch byte is bounded by vault::TAG_SWATCH_COUNT; the palette
// it indexes lives in gfx. This is the one place both are visible, so it is the
// place to assert they have not drifted apart.
static_assert(gfx::TAG_SWATCH_COUNT == static_cast<int>(vault::TAG_SWATCH_COUNT),
              "gfx tag palette size must match the persisted swatch bound");

namespace {

// Chips that fit back-to-back in `avail_w`, ignoring any counter, plus the
// pixels they use. The shared core of fit_chips and pack_chip_lines.
struct GreedyRun { int shown = 0; float used = 0.0f; };

GreedyRun greedy_run(std::span<const int> widths, float avail_w) noexcept
{
    GreedyRun out;
    for (size_t i = 0; i < widths.size(); ++i) {
        const float advance = (i == 0 ? 0.0f : ui::CHIP_SPACING) + static_cast<float>(widths[i]);
        if (out.used + advance > avail_w) {
            break;
        }
        out.used += advance;
        ++out.shown;
    }
    return out;
}

// Chips placed by a completed pass: the lines are contiguous from index 0, so
// the end of the last one is the count.
int placed(const std::vector<ui::ChipLine>& lines) noexcept
{
    return lines.empty() ? 0 : lines.back().first + lines.back().count;
}

// Greedy fill into `line_w`, at most `max_lines` lines. A non-positive `line_w`
// simply places nothing.
std::vector<ui::ChipLine> pack_pass(std::span<const int> widths, float line_w, int max_lines)
{
    std::vector<ui::ChipLine> lines;
    const int                 n = static_cast<int>(widths.size());
    int                       i = 0;
    while (i < n && std::cmp_less(lines.size(), max_lines)) {
        const auto g = greedy_run(widths.subspan(static_cast<size_t>(i)), line_w);
        if (g.shown == 0) {
            break;
        }
        lines.push_back({.first = i, .count = g.shown, .width = g.used});
        i += g.shown;
    }
    return lines;
}

}  // namespace

namespace ui {

ChipFit fit_chips(std::span<const int> chip_widths, float avail_w, float overflow_w) noexcept
{
    const int n = static_cast<int>(chip_widths.size());
    if (n == 0) {
        return {.shown = 0, .hidden = 0};
    }

    const GreedyRun g = greedy_run(chip_widths, avail_w);
    if (g.shown == n) {
        return {.shown = g.shown, .hidden = 0};
    }

    // Room must also be found for the "+N" counter; drop shown chips until it
    // fits.
    int   shown = g.shown;
    float used = g.used;
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

float lone_chip_text_w(float max_w, float overflow_w, int hidden_after) noexcept
{
    float w = max_w - (CHIP_DOT + CHIP_GAP);
    if (hidden_after > 0) {
        w -= CHIP_SPACING + overflow_w;
    }
    return w;
}

ChipWrap pack_chip_lines(std::span<const int> chip_widths, float max_w,
                         int max_lines, float overflow_w)
{
    const int n = static_cast<int>(chip_widths.size());

    ChipWrap out;
    out.lines  = pack_pass(chip_widths, max_w, max_lines);
    out.hidden = n - placed(out.lines);
    if (out.hidden == 0) {
        return out;
    }

    // Chips are left over, so a "+N" will be drawn right-aligned within `max_w`.
    // Repack into the width that leaves room for it so no chip can collide.
    ChipWrap tight;
    tight.lines  = pack_pass(chip_widths, max_w - CHIP_SPACING - overflow_w, max_lines);
    tight.hidden = n - placed(tight.lines);
    return tight;
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
    const auto        overflow_w = static_cast<float>(font.measure(overflow_probe));
    const auto        fit = fit_chips(widths, max_w, overflow_w);

    // The dot and the text share one vertical centre line. text_top_for_center
    // uses the baked glyphs' real ink extents, so this stays aligned regardless
    // of the font's nominal pixel height — do not hand-compute a baseline.
    const float center_y = y + CHIP_ROW_H * 0.5f;
    const float text_y   = font.text_top_for_center(center_y);

    if (fit.shown == 0) {
        // Not one whole chip fits. A bare "+N" would drop the name entirely, so
        // draw the first tag with its text middle-elided and count the rest.
        const int         hidden_after = static_cast<int>(tags.size()) - 1;
        const TagDisplay& d    = shown_tags.front();
        const std::string text = fit_text(font, d.text,
                                          lone_chip_text_w(max_w, overflow_w, hidden_after));
        if (!text.empty()) {
            const gfx::Color c = d.swatch < 0 ? gfx::theme::TEXT_DIM : gfx::tag_swatch(d.swatch);
            const SDL_FRect  dot{.x = x,
                                 .y = center_y - CHIP_DOT * 0.5f,
                                 .w = CHIP_DOT,
                                 .h = CHIP_DOT};
            r.draw_round_rect(dot, CHIP_DOT * 0.5f, c);
            r.draw_text(font, x + CHIP_DOT + CHIP_GAP, text_y, text, c);
            if (hidden_after > 0) {
                r.draw_text(font, x + max_w - overflow_w, text_y,
                            std::format("+{}", hidden_after), gfx::theme::TEXT_FAINT);
            }
        } else {
            // Not even an ellipsis fits: fall back to a bare counter. `tags` is
            // known non-empty here — the early return above excluded that case.
            r.draw_text(font, x, text_y, std::format("+{}", tags.size()), gfx::theme::TEXT_FAINT);
        }
        return;
    }

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

bool any_chips_to_show(std::span<const vault::IndexNode* const> children) noexcept
{
    return std::ranges::any_of(children, [](const vault::IndexNode* n) {
        return n != nullptr && !n->tags.empty();
    });
}

} // namespace ui
