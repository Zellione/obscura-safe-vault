#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"

namespace ui {

bool point_in_rect(float x, float y, const SDL_FRect& r) noexcept
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

int grid_columns(float avail_w, float cell, float gap) noexcept
{
    if (cell <= 0.0f) return 1;
    const auto cols = static_cast<int>(std::floor((avail_w + gap) / (cell + gap)));
    return cols < 1 ? 1 : cols;
}

SDL_FRect grid_cell_rect(int index, const GridSpec& g) noexcept
{
    const int cols = g.cols < 1 ? 1 : g.cols;
    const int row = index / cols;
    const int col = index % cols;
    return SDL_FRect{g.origin_x + static_cast<float>(col) * (g.cell + g.gap),
                     g.origin_y + static_cast<float>(row) * (g.cell + g.gap),
                     g.cell, g.cell};
}

int grid_hit(float mx, float my, int count, const GridSpec& g) noexcept
{
    for (int i = 0; i < count; ++i)
        if (point_in_rect(mx, my, grid_cell_rect(i, g)))
            return i;
    return -1;
}

SDL_FRect fit_rect(float w, float h, const SDL_FRect& box) noexcept
{
    if (w <= 0.0f || h <= 0.0f) return box;
    const float scale = std::min(box.w / w, box.h / h);
    const float nw = w * scale;
    const float nh = h * scale;
    return SDL_FRect{box.x + (box.w - nw) * 0.5f, box.y + (box.h - nh) * 0.5f, nw, nh};
}

void draw_button(gfx::Renderer& r, gfx::FontAtlas& font, const Button& b,
                 bool hover, bool active)
{
    using namespace gfx::theme;
    gfx::Color bg = SURFACE;
    if (active)     bg = ACCENT_DIM;
    else if (hover) bg = SURFACE_HI;
    r.draw_round_rect(b.rect, RADIUS_SMALL, bg);
    r.draw_round_rect(b.rect, RADIUS_SMALL, (hover || active) ? ACCENT : BORDER,
                      /*filled*/ false);
    const int tw = font.measure(b.label);
    r.draw_text(font, b.rect.x + (b.rect.w - static_cast<float>(tw)) * 0.5f,
                font.text_top_for_center(b.rect.y + b.rect.h * 0.5f), b.label, TEXT);
}

void draw_text_field(gfx::Renderer& r, gfx::FontAtlas& font, const SDL_FRect& box,
                     std::string_view shown, bool focused)
{
    using namespace gfx::theme;
    r.draw_round_rect(box, RADIUS_SMALL, SURFACE);
    r.draw_round_rect(box, RADIUS_SMALL, focused ? ACCENT : BORDER, /*filled*/ false);
    r.draw_text(font, box.x + 12.0f, font.text_top_for_center(box.y + box.h * 0.5f),
                shown, TEXT);
}

ButtonState button_state(const SDL_FRect& rect, float mx, float my,
                         bool mouse_down) noexcept
{
    const bool hover = point_in_rect(mx, my, rect);
    return {hover, hover && mouse_down};
}

} // namespace ui
