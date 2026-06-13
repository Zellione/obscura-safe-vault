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

} // namespace ui
