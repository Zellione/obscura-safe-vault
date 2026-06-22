#include "ui/cover_layout.h"

#include <algorithm>
#include <array>

namespace ui {

std::vector<SDL_FRect> cover_montage_rects(const SDL_FRect& box, int count,
                                           float gap) noexcept
{
    if (count <= 0) return {};
    if (count == 1) return {box};

    count = std::min(count, 4);
    const float cw = (box.w - gap) * 0.5f;
    const float ch = (box.h - gap) * 0.5f;

    // Row-major 2×2 grid cells: TL, TR, BL, BR — take the first `count`.
    const std::array<SDL_FRect, 4> cells{{
        {box.x,            box.y,            cw, ch},
        {box.x + cw + gap, box.y,            cw, ch},
        {box.x,            box.y + ch + gap, cw, ch},
        {box.x + cw + gap, box.y + ch + gap, cw, ch},
    }};

    std::vector<SDL_FRect> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) out.push_back(cells[i]);
    return out;
}

}  // namespace ui
