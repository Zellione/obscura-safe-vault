#include "ui/detail_panel.h"

#include <algorithm>
#include <format>
#include <span>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "ui/detail_layout.h"
#include "ui/tag_chip.h"
#include "ui/widgets.h"   // fit_text

namespace ui {
namespace {

constexpr float PAD = 16.0f;   // inner left/right padding

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

    const float x       = rect.x + PAD;
    const float start_y = rect.y + PAD - scroll;

    LineCtx ctx{.r = r, .font = font, .rect = rect, .x = x};

    const DetailMetrics m = detail_metrics(font.pixel_height());
    const auto lines = layout_detail_lines(content, m);

    for (const DetailLine& l : lines) {
        const float y = start_y + l.y;
        if (y + l.height <= rect.y || y >= rect.y + rect.h) continue;   // cull off-panel
        switch (l.kind) {
            using enum DetailLineKind;
            case Heading:
                (void)line(ctx, y, l.height, content.heading, TEXT);
                break;
            case Subheading:
                (void)line(ctx, y, l.height, content.subheading, FAVORITE);
                break;
            case SectionTitle:
            case TagSectionTitle:
                (void)line(ctx, y, l.height, content.sections[l.section].title, TEXT_FAINT);
                break;
            case Row: {
                const DetailRow& row = content.sections[l.section].rows[l.item];
                (void)line(ctx, y, l.height, std::format("{}  {}", row.label, row.value), TEXT_DIM);
                break;
            }
            case Bullet:
                (void)line(ctx, y, l.height,
                           std::format("• {}", content.sections[l.section].bullets[l.item]),
                           TEXT_DIM);
                break;
            case TagBullet: {
                const std::string& tag = content.sections[l.section].bullets[l.item];
                draw_tag_chips(r, font, x, y + (m.chip_line_h - CHIP_ROW_H) * 0.5f,
                               rect.w - (2.0f * PAD), std::span(&tag, 1), categories);
                break;
            }
        }
    }
    return detail_content_height(lines) + PAD;
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
