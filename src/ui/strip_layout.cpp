#include "ui/strip_layout.h"

#include <algorithm>
#include <cmath>

namespace ui {

float strip_thumb_size(float win_h) noexcept
{
    const float bottom_h = win_h * STRIP_FRACTION;
    return std::max(8.0f, (bottom_h - 2.0f * STRIP_MARGIN) * 0.5f);
}

float left_strip_width(float thumb) noexcept
{
    return thumb + 2.0f * STRIP_PAD;
}

// Bottom bar thickness: just the thumbnail plus a little padding — no dead space.
static float bottom_strip_height(float thumb) noexcept
{
    return thumb + 2.0f * STRIP_PAD;
}

SDL_FRect viewport_rect_for(StripSide side, float win_w, float win_h,
                            float thumb) noexcept
{
    if (side == StripSide::Left) {
        const float lw = left_strip_width(thumb);
        return SDL_FRect{lw, 0.0f, win_w - lw, win_h};
    }
    return SDL_FRect{0.0f, 0.0f, win_w, win_h - bottom_strip_height(thumb)};
}

SDL_FRect strip_rect_for(StripSide side, float win_w, float win_h,
                         float thumb) noexcept
{
    if (side == StripSide::Left)
        return SDL_FRect{0.0f, 0.0f, left_strip_width(thumb), win_h};

    const float bar = bottom_strip_height(thumb);
    return SDL_FRect{0.0f, win_h - bar, win_w, bar};
}

int strip_hit_axis(float along, float origin_along, float scroll, float thumb,
                   float gap, int count) noexcept
{
    for (int i = 0; i < count; ++i) {
        const float start = origin_along - scroll + static_cast<float>(i) * (thumb + gap);
        if (along >= start && along <= start + thumb) return i;
    }
    return -1;
}

SDL_FRect strip_cell_rect(int index, const SDL_FRect& strip, float thumb,
                          float gap, float scroll, bool vertical) noexcept
{
    // Lay cells out along the strip's long axis; centre them on the short axis.
    // SYNC: this geometry must match gfx::Renderer::draw_thumbnail_strip
    // (src/gfx/renderer.cpp). Both independently compute cell rects to avoid
    // coupling gfx and ui modules; if layout changes, update both sites.
    const float cross  = vertical ? strip.x + (strip.w - thumb) * 0.5f
                                  : strip.y + (strip.h - thumb) * 0.5f;
    const float origin = vertical ? strip.y : strip.x;
    const float along  = origin - scroll + static_cast<float>(index) * (thumb + gap);

    if (vertical) {
        return SDL_FRect{cross, along, thumb, thumb};
    } else {
        return SDL_FRect{along, cross, thumb, thumb};
    }
}

StripRange strip_visible_range(float scroll, float extent, float thumb,
                               float gap, int count, int margin) noexcept
{
    if (count <= 0 || extent <= 0.0f || thumb <= 0.0f) return {};
    const float pitch = thumb + gap;
    // Cell i spans [i*pitch - scroll, i*pitch - scroll + thumb) on screen.
    // Visibility is strict on both edges: a cell ending exactly at 0 or
    // starting exactly at `extent` shows zero pixels and is excluded.
    const int first = static_cast<int>(std::floor((scroll - thumb) / pitch)) + 1;
    const int last  = static_cast<int>(std::ceil((scroll + extent) / pitch)) - 1;
    return {std::max(0, first - margin), std::min(count - 1, last + margin)};
}

SDL_FRect strip_counter_rect(StripSide side, SDL_FRect strip, float text_w,
                             float line_h) noexcept
{
    constexpr float PAD = 8.0f;
    const float badge_w = text_w + 2.0f * PAD;
    const float badge_h = line_h + 2.0f * PAD;

    if (side == StripSide::Bottom) {
        // Bottom strip: badge at right edge, vertically centered.
        const float x = strip.x + strip.w - PAD - badge_w;
        const float y = strip.y + (strip.h - badge_h) * 0.5f;
        return SDL_FRect{x, y, badge_w, badge_h};
    } else {
        // Left strip: badge at bottom-right corner.
        const float x = strip.x + strip.w - PAD - badge_w;
        const float y = strip.y + strip.h - PAD - badge_h;
        return SDL_FRect{x, y, badge_w, badge_h};
    }
}

SDL_FRect fullscreen_counter_rect(float win_w, float win_h, float text_w,
                                  float line_h) noexcept
{
    constexpr float MARGIN = 12.0f;
    constexpr float PAD = 8.0f;
    const float badge_w = text_w + 2.0f * PAD;
    const float badge_h = line_h + 2.0f * PAD;

    // Bottom-right corner with MARGIN from edges.
    const float x = win_w - MARGIN - badge_w;
    const float y = win_h - MARGIN - badge_h;
    return SDL_FRect{x, y, badge_w, badge_h};
}

} // namespace ui
