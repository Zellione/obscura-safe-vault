#include "ui/widgets.h"

#include <algorithm>
#include <cmath>

#include "gfx/renderer.h"
#include "gfx/text.h"

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
    gfx::Color bg{55, 55, 70, 255};
    if (active)     bg = gfx::Color{90, 60, 150, 255};
    else if (hover) bg = gfx::Color{70, 70, 95, 255};
    r.draw_rect(b.rect, bg);
    r.draw_rect(b.rect, gfx::Color{120, 80, 200, 255}, /*filled*/ false);
    const int tw = font.measure(b.label);
    r.draw_text(font, b.rect.x + (b.rect.w - static_cast<float>(tw)) * 0.5f,
                b.rect.y + b.rect.h * 0.5f - 14.0f, b.label,
                gfx::Color{235, 235, 240, 255});
}

void draw_text_field(gfx::Renderer& r, gfx::FontAtlas& font, const SDL_FRect& box,
                     std::string_view shown, bool focused)
{
    r.draw_rect(box, gfx::Color{40, 40, 50, 255});
    r.draw_rect(box, focused ? gfx::Color{120, 80, 200, 255}
                             : gfx::Color{80, 80, 95, 255}, /*filled*/ false);
    r.draw_text(font, box.x + 10.0f, box.y + box.h * 0.5f - 14.0f, shown,
                gfx::Color{230, 230, 235, 255});
}

} // namespace ui
