#include "ui/dual_layout.h"

#include <algorithm>
#include <cmath>

namespace ui {

DualSplit dual_split(float win_w, float win_h) noexcept
{
    const float w   = std::max(0.0f, win_w);
    const float h   = std::max(0.0f, win_h);
    const float div = std::min(2.0f, w);                       // divider strip
    const float lw  = std::floor((w - div) / 2.0f);
    const float rw  = w - div - lw;
    DualSplit s;
    s.left    = {0.0f, 0.0f, lw, h};
    s.divider = {lw, 0.0f, div, h};
    s.right   = {lw + div, 0.0f, rw, h};
    return s;
}

int pane_at(const DualSplit& s, float x) noexcept
{
    return x < s.divider.x + s.divider.w / 2.0f ? 0 : 1;
}

} // namespace ui
