#pragma once

#include <SDL3/SDL.h>

// Pure, SDL-geometry-only layout for the fixed chrome bands — a header at the
// top and a footer at the bottom — that frame a screen's scrollable or zoomable
// content area.
//
// The bands are OPAQUE and RESERVED: content is laid out strictly between them,
// never underneath. That is the whole point of this helper — a translucent band
// painted over the content leaves its text washed out by whatever scrolls or
// zooms behind it, and hides the content it covers. Reserving the space instead
// makes the text legible against a solid fill and keeps every pixel of the
// image, video or tile grid visible.
//
// Headless / unit-testable.
namespace ui {

// Header/footer band rects plus the content area left between them. All three
// share `area`'s x and width; a band with no height comes back with h == 0 at
// the corresponding edge.
struct ChromeBands {
    SDL_FRect header;
    SDL_FRect content;
    SDL_FRect footer;
};

// Split `area` into a `header_h`-tall top band, a `footer_h`-tall bottom band
// and the content area between them. Negative heights are treated as 0. When
// the two bands do not both fit in `area`, they are shrunk proportionally so
// they still cover `area` exactly and the content area collapses to zero height
// rather than inverting.
[[nodiscard]] ChromeBands split_chrome(const SDL_FRect& area, float header_h,
                                       float footer_h) noexcept;

} // namespace ui
