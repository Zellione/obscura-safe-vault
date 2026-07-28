#include "ui/text_metrics.h"

#include <algorithm>
#include <cmath>

namespace ui {

float line_pitch(float font_px) noexcept
{
    if (font_px <= 0.0f) return 0.0f;
    return std::ceil(font_px * 1.25f);
}

float row_height(float font_px, float pad) noexcept
{
    return line_pitch(font_px) + 2.0f * std::max(0.0f, pad);
}

} // namespace ui
