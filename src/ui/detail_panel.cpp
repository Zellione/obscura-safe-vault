#include "ui/detail_panel.h"

#include <algorithm>
#include <format>
#include <span>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "ui/tag_chip.h"
#include "ui/widgets.h"   // fit_text

namespace ui {
namespace {

constexpr float PAD          = 16.0f;   // inner left/right padding
constexpr float ROW_H        = 24.0f;   // one label/value or bullet line
constexpr float HEADING_H    = 34.0f;
constexpr float SUBHEADING_H = 24.0f;
constexpr float SECTION_GAP  = 18.0f;   // space above a section title
constexpr float TITLE_H      = 22.0f;

// Per-draw invariants shared by every line in one draw_detail_panel call.
struct LineCtx {
    gfx::Renderer&   r;
    gfx::FontAtlas&  font;
    const SDL_FRect& rect;
    float            x;
};

// Draw one line if it falls inside `ctx.rect`; always returns the next y.
float line(const LineCtx& ctx, float y, float height, std::string_view text, gfx::Color c)
{
    if (y + height > ctx.rect.y && y < ctx.rect.y + ctx.rect.h) {
        ctx.r.draw_text(ctx.font, ctx.x, y, fit_text(ctx.font, text, ctx.rect.w - (2.0f * PAD)), c);
    }
    return y + height;
}

}  // namespace

float detail_panel_width(bool open, float window_width) noexcept
{
    if (!open) {
        return 0.0f;
    }
    if (window_width < DETAIL_PANEL_MIN_WINDOW) {
        return 0.0f;
    }
    return DETAIL_PANEL_WIDTH;
}

float draw_detail_panel(gfx::Renderer& r, gfx::FontAtlas& font, const SDL_FRect& rect,
                        const DetailContent& content, float scroll,
                        std::span<const vault::TagCategory> categories)
{
    using namespace gfx::theme;

    r.draw_rect(rect, SURFACE);
    r.draw_rect({.x = rect.x, .y = rect.y, .w = 1.0f, .h = rect.h}, BORDER);   // hairline against the grid

    const float x = rect.x + PAD;
    float       y = rect.y + PAD - scroll;
    const float start_y = y;

    LineCtx ctx{.r = r, .font = font, .rect = rect, .x = x};

    y = line(ctx, y, HEADING_H, content.heading, TEXT);
    if (!content.subheading.empty()) {
        y = line(ctx, y, SUBHEADING_H, content.subheading, FAVORITE);
    }

    for (const auto& s : content.sections) {
        y += SECTION_GAP;
        if (!s.title.empty()) {
            y = line(ctx, y, TITLE_H, s.title, TEXT_FAINT);
        }
        for (const auto& row : s.rows) {
            y = line(ctx, y, ROW_H,
                     std::format("{}  {}", row.label, row.value), TEXT_DIM);
        }
        for (const auto& b : s.bullets) {
            if (s.is_tags) {
                if (y + CHIP_ROW_H > rect.y && y < rect.y + rect.h) {
                    draw_tag_chips(r, font, x, y, rect.w - (2.0f * PAD),
                                   std::span(&b, 1), categories);
                }
                y += CHIP_ROW_H;
            } else {
                y = line(ctx, y, ROW_H, std::format("• {}", b), TEXT_DIM);
            }
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
        st.scroll = std::max(0.0f, st.scroll - DETAIL_PANEL_SCROLL_STEP);
        return true;
    }
    if (key.key == SDLK_DOWN) {
        st.scroll += DETAIL_PANEL_SCROLL_STEP;
        return true;
    }
    return false;
}

bool detail_panel_hit(bool open, float window_width, float mouse_x) noexcept
{
    const float w = detail_panel_width(open, window_width);
    if (w <= 0.0f) {
        return false;
    }
    return mouse_x >= window_width - w;
}

void scroll_detail_panel(DetailPanelState& st, float wheel_y) noexcept
{
    st.scroll = std::max(0.0f, st.scroll - wheel_y * DETAIL_PANEL_SCROLL_STEP);
}

}  // namespace ui
