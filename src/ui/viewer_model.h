#pragma once

// Pure zoom / pan / thumbnail-strip math for the image viewer (Phase 6).
//
// This unit is deliberately free of SDL and any GPU/IO so it can be unit-tested
// headlessly (matching nav_model / unlock_logic / widgets). The ImageViewer
// screen owns the SDL plumbing and delegates every numeric decision here.
//
// Coordinate model for zoom/pan:
//   * The viewport is a rectangle (vw x vh); its centre is the anchor.
//   * `zoom` scales the image's natural size: scaled = (iw*zoom, ih*zoom).
//   * `pan` offsets the image centre from the viewport centre, in screen pixels.
//   So the image's on-screen top-left is:
//       (vw/2 + pan.x - scaled_w/2,  vh/2 + pan.y - scaled_h/2)

#include <algorithm>

namespace ui {

// Zoom limits, matching the ROADMAP acceptance criterion (5% .. 2000%).
inline constexpr float ZOOM_MIN = 0.05f;
inline constexpr float ZOOM_MAX = 20.0f;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// Clamp a zoom factor into [ZOOM_MIN, ZOOM_MAX].
[[nodiscard]] constexpr float clamp_zoom(float zoom) noexcept
{
    return std::clamp(zoom, ZOOM_MIN, ZOOM_MAX);
}

// Fit-to-window scale for a (iw x ih) image inside a (vw x vh) viewport. Returns
// 1.0 for a degenerate image. Not pre-clamped — callers pass it through
// clamp_zoom.
[[nodiscard]] constexpr float fit_zoom(float iw, float ih, float vw, float vh) noexcept
{
    if (iw <= 0.0f || ih <= 0.0f) return 1.0f;
    return std::min(vw / iw, vh / ih);
}

// Step `index` by `delta` within [0, count), wrapping at both ends. Returns 0 for
// an empty list. Used for Left/Right navigation that wraps first<->last.
[[nodiscard]] constexpr int wrap_index(int index, int delta, int count) noexcept
{
    if (count <= 0) return 0;
    int r = (index + delta) % count;
    if (r < 0) r += count;
    return r;
}

// Clamp `pan` so the image can never be dragged entirely off-screen. When the
// scaled image is larger than the viewport on an axis it stays covering it (no
// background gap); when smaller it stays fully inside. Both reduce to the same
// symmetric bound |pan| <= |scaled - view| / 2.
[[nodiscard]] constexpr Vec2 clamp_pan(Vec2 pan, float scaled_w, float scaled_h,
                                       float vw, float vh) noexcept
{
    // Not std::abs: MSVC's <cmath> doesn't implement C++23 constexpr abs yet.
    const float dx = scaled_w - vw;
    const float dy = scaled_h - vh;
    const float lx = (dx < 0.0f ? -dx : dx) * 0.5f;
    const float ly = (dy < 0.0f ? -dy : dy) * 0.5f;
    return Vec2{std::clamp(pan.x, -lx, lx), std::clamp(pan.y, -ly, ly)};
}

struct ZoomResult {
    float zoom = 1.0f;
    Vec2  pan;
};

// Zoom by `factor` while keeping the image point currently under the cursor
// (`cursor`, in viewport coords) fixed on screen. `img` is the image's natural
// size, `view` the viewport size. The resulting zoom is clamped to
// [ZOOM_MIN, ZOOM_MAX] and the resulting pan is clamped via clamp_pan.
[[nodiscard]] inline ZoomResult zoom_at(Vec2 img, float zoom, Vec2 pan, float factor,
                                        Vec2 cursor, Vec2 view) noexcept
{
    const float nz = clamp_zoom(zoom * factor);
    if (img.x <= 0.0f || img.y <= 0.0f || zoom <= 0.0f)
        return ZoomResult{nz, pan};

    // Image-space (natural px) coordinate currently under the cursor.
    const float draw_x = view.x * 0.5f + pan.x - img.x * zoom * 0.5f;
    const float draw_y = view.y * 0.5f + pan.y - img.y * zoom * 0.5f;
    const float u = (cursor.x - draw_x) / zoom;
    const float v = (cursor.y - draw_y) / zoom;

    // Pin (u, v) under the cursor at the new zoom and back out the pan.
    const float ndraw_x = cursor.x - u * nz;
    const float ndraw_y = cursor.y - v * nz;
    Vec2 npan{ndraw_x - view.x * 0.5f + img.x * nz * 0.5f,
              ndraw_y - view.y * 0.5f + img.y * nz * 0.5f};
    return ZoomResult{nz, clamp_pan(npan, img.x * nz, img.y * nz, view.x, view.y)};
}

// Total content width of `count` thumbnails of side `thumb` separated by `gap`.
[[nodiscard]] constexpr float strip_content_width(int count, float thumb,
                                                  float gap) noexcept
{
    if (count <= 0) return 0.0f;
    return static_cast<float>(count) * thumb + static_cast<float>(count - 1) * gap;
}

// Horizontal scroll offset (>= 0) that centres thumbnail `selected` within a
// strip of width `strip_w`, clamped so the strip never scrolls past the content
// edges. With the result, cell i sits at content-x i*(thumb+gap) minus scroll.
[[nodiscard]] inline float strip_scroll_centered(int selected, int count,
                                                 float thumb, float gap,
                                                 float strip_w) noexcept
{
    if (count <= 0) return 0.0f;
    const float content   = strip_content_width(count, thumb, gap);
    const float cell_ctr  = static_cast<float>(selected) * (thumb + gap) + thumb * 0.5f;
    const float max_scroll = std::max(0.0f, content - strip_w);
    return std::clamp(cell_ctr - strip_w * 0.5f, 0.0f, max_scroll);
}

} // namespace ui
