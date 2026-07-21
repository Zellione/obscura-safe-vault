#include "ui/detail_panel.h"

#include <algorithm>
#include <format>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "ui/widgets.h"   // fit_text

namespace ui {
namespace {

constexpr float PAD          = 16.0F;   // inner left/right padding
constexpr float ROW_H        = 24.0F;   // one label/value or bullet line
constexpr float HEADING_H    = 34.0F;
constexpr float SUBHEADING_H = 24.0F;
constexpr float SECTION_GAP  = 18.0F;   // space above a section title
constexpr float TITLE_H      = 22.0F;

// Draw one line if it falls inside `rect`; always returns the next y.
float line(gfx::Renderer& r, gfx::FontAtlas& font, const SDL_FRect& rect, float x, float y,
           float height, std::string_view text, gfx::Color c)
{
    if (y + height > rect.y && y < rect.y + rect.h) {
        r.draw_text(font, x, y, fit_text(font, text, rect.w - (2.0F * PAD)), c);
    }
    return y + height;
}

}  // namespace

float detail_panel_width(bool open, float window_width) noexcept
{
    if (!open) {
        return 0.0F;
    }
    if (window_width < DETAIL_PANEL_MIN_WINDOW) {
        return 0.0F;
    }
    return DETAIL_PANEL_WIDTH;
}

float draw_detail_panel(gfx::Renderer& r, gfx::FontAtlas& font, const SDL_FRect& rect,
                        const DetailContent& content, float scroll)
{
    using namespace gfx::theme;

    r.draw_rect(rect, SURFACE);
    r.draw_rect({.x = rect.x, .y = rect.y, .w = 1.0F, .h = rect.h}, BORDER);   // hairline against the grid

    const float x = rect.x + PAD;
    float       y = rect.y + PAD - scroll;
    const float start_y = y;

    y = line(r, font, rect, x, y, HEADING_H, content.heading, TEXT);
    if (!content.subheading.empty()) {
        y = line(r, font, rect, x, y, SUBHEADING_H, content.subheading, FAVORITE);
    }

    for (const auto& s : content.sections) {
        y += SECTION_GAP;
        if (!s.title.empty()) {
            y = line(r, font, rect, x, y, TITLE_H, s.title, TEXT_FAINT);
        }
        for (const auto& row : s.rows) {
            y = line(r, font, rect, x, y, ROW_H,
                     std::format("{}  {}", row.label, row.value), TEXT_DIM);
        }
        for (const auto& b : s.bullets) {
            y = line(r, font, rect, x, y, ROW_H, std::format("• {}", b), TEXT_DIM);
        }
    }
    return (y - start_y) + PAD;
}

bool handle_detail_panel_scroll(const SDL_KeyboardEvent& key, DetailPanelState& st)
{
    if (!st.open) {
        return false;
    }
    if ((key.mod & SDL_KMOD_CTRL) == 0) {
        return false;
    }
    if (key.key == SDLK_UP) {
        st.scroll = std::max(0.0F, st.scroll - DETAIL_PANEL_SCROLL_STEP);
        return true;
    }
    if (key.key == SDLK_DOWN) {
        st.scroll += DETAIL_PANEL_SCROLL_STEP;
        return true;
    }
    return false;
}

}  // namespace ui
