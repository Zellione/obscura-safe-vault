#pragma once

#include <SDL3/SDL.h>

// Pure, SDL-geometry-only layout for the image-viewer thumbnail strip. The strip
// can sit at the Bottom (horizontal) or on the Left (vertical); these helpers
// give the viewport/strip rectangles, the (halved) thumbnail size, and
// orientation-independent hit-testing. Headless / unit-testable.
namespace ui {

enum class StripSide { Bottom, Left };

// STRIP_FRACTION only sizes the thumbnail (see strip_thumb_size); it is NOT the
// bar's on-screen size. The bar itself hugs the thumbnails: it is exactly
// `thumb + 2*STRIP_PAD` thick, so there is no dead space around them.
inline constexpr float STRIP_FRACTION = 0.25f;  // used only to derive thumb size
inline constexpr float STRIP_MARGIN   = 16.0f;  // thumb-size derivation only
inline constexpr float STRIP_PAD      = 10.0f;  // padding between thumbs and bar edges
inline constexpr float STRIP_GAP      = 10.0f;  // gap between thumbnails
inline constexpr int   STRIP_PREFETCH_CELLS = 8;  // fetch margin beyond the visible window

// Thumbnail side length: HALF the cross-axis space of the bottom strip (the
// "half size" overhaul). Shared by both orientations so a thumb is the same size
// however the strip is turned. Never below 8px.
[[nodiscard]] float strip_thumb_size(float win_h) noexcept;

// Width of the vertical Left strip column: a thumb plus margins on both sides.
[[nodiscard]] float left_strip_width(float thumb) noexcept;

// Image viewport rectangle for the given strip side.
[[nodiscard]] SDL_FRect viewport_rect_for(StripSide side, float win_w, float win_h,
                                          float thumb) noexcept;

// Thumbnail strip rectangle for the given strip side.
[[nodiscard]] SDL_FRect strip_rect_for(StripSide side, float win_w, float win_h,
                                       float thumb) noexcept;

// Index of the thumbnail whose cell contains position `along` (x for Bottom, y
// for Left), where the first cell starts at `origin_along` and the strip is
// scrolled by `scroll`. Returns -1 when `along` falls in a gap or outside.
[[nodiscard]] int strip_hit_axis(float along, float origin_along, float scroll,
                                 float thumb, float gap, int count) noexcept;

// Rectangle for thumbnail at the given index within a strip. The strip's
// orientation (horizontal/vertical) is determined by the `vertical` flag.
// Used by both the renderer and badge drawing to ensure consistent layout.
[[nodiscard]] SDL_FRect strip_cell_rect(int index, const SDL_FRect& strip, float thumb,
                                        float gap, float scroll, bool vertical) noexcept;

// Inclusive [first, last] range of thumbnail indices whose cells intersect a
// strip window of `extent` pixels scrolled by `scroll`, widened by `margin`
// cells on each side (clamped to [0, count-1]). Empty when first > last.
// Exists so the viewer requests/fetches ONLY near-visible thumbnails — asking
// for the whole album floods the background fetch worker with the entire
// vault's thumb I/O and starves video streaming off the same disk.
struct StripRange {
    int first = 0;
    int last  = -1;
};
[[nodiscard]] StripRange strip_visible_range(float scroll, float extent, float thumb,
                                             float gap, int count, int margin) noexcept;

// Rectangle for the position counter badge on the thumbnail strip (Phase 68).
// Badge is 8px padding around the text, positioned at the strip's far edge.
// For Bottom strip: right edge, vertically centered.
// For Left strip: bottom-right corner.
[[nodiscard]] SDL_FRect strip_counter_rect(StripSide side, SDL_FRect strip, float text_w,
                                           float line_h) noexcept;

// Rectangle for the position counter badge in fullscreen mode (Phase 68).
// Badge is 8px padding around the text, positioned at the window's bottom-right
// corner with 12px margin.
[[nodiscard]] SDL_FRect fullscreen_counter_rect(float win_w, float win_h, float text_w,
                                                float line_h) noexcept;

} // namespace ui
