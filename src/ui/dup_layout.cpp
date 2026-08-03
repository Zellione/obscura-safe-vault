#include "ui/dup_layout.h"

#include <algorithm>

#include "ui/text_metrics.h"

namespace ui {

DupRowLayout dup_row_layout(float content_x, float content_w, float tile_h,
                            float font_px, std::size_t member_count) noexcept
{
    const auto n = static_cast<float>(std::max<std::size_t>(member_count, 1));
    const float pitch = line_pitch(font_px);

    DupRowLayout l;
    l.tile_h = tile_h;
    l.tile_w = std::max(DUP_MIN_TILE_W, (content_w - (n - 1.0f) * DUP_TILE_GAP) / n);

    const float block_w = n * l.tile_w + (n - 1.0f) * DUP_TILE_GAP;
    l.first_x = content_x + std::max(0.0f, (content_w - block_w) * 0.5f);

    l.header_h = pitch + 8.0f;
    l.text_h   = 3.0f * pitch + 8.0f;
    l.row_h    = l.header_h + l.tile_h + l.text_h + DUP_GROUP_GAP;
    return l;
}

float dup_tile_height(float window_h) noexcept
{
    // ~150 list top + ~120 footer band; see duplicates_screen.cpp.
    return std::clamp((window_h - 270.0f) * 0.5f, 180.0f, 440.0f);
}

float dup_footer_height(float font_px) noexcept
{
    return 3.0f * line_pitch(font_px) + 10.0f;
}

} // namespace ui
