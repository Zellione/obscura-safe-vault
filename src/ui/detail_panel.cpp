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

// Draw a single bullet; return the new y position.
float draw_bullet(const LineCtx& ctx, float y, bool is_tag, const std::string& bullet,
                  std::span<const vault::TagCategory> categories)
{
    if (is_tag) {
        // A chip line reserves a FULL text line (CHIP_LINE_H): the ~28 px glyph
        // ink would otherwise bleed into the row above and below. The content is
        // centred within that box, matching the rhythm of the text rows around it.
        if (y + CHIP_LINE_H > ctx.rect.y && y < ctx.rect.y + ctx.rect.h) {
            draw_tag_chips(ctx.r, ctx.font, ctx.x, y + (CHIP_LINE_H - CHIP_ROW_H) * 0.5f,
                          ctx.rect.w - (2.0f * PAD), std::span(&bullet, 1), categories);
        }
        return y + CHIP_LINE_H;
    }
    // Draw regular text bullet.
    return line(ctx, y, ROW_H, std::format("• {}", bullet), gfx::theme::TEXT_DIM);
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
            // A tag section's title reserves a full line so the first chip's ink
            // clears the title's descenders; metadata titles keep their tighter
            // TITLE_H so existing sections look unchanged.
            y = line(ctx, y, s.is_tags ? CHIP_LINE_H : TITLE_H, s.title, TEXT_FAINT);
        }
        for (const auto& row : s.rows) {
            y = line(ctx, y, ROW_H,
                     std::format("{}  {}", row.label, row.value), TEXT_DIM);
        }
        for (const auto& b : s.bullets) {
            y = draw_bullet(ctx, y, s.is_tags, b, categories);
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
