#pragma once

#include <SDL3/SDL.h>

#include <vector>

// Pure, SDL-geometry-only montage layout for gallery cover tiles (Phase 19).
// Given a tile's content box and how many covers it has (1–4), it returns the
// sub-rectangles each cover is drawn into. Headless / unit-testable; no decode,
// no vault, no disk.
namespace ui {

// Montage sub-rects for `count` covers inside `box`:
//   count == 1 -> a single rect filling the whole box.
//   count 2–4  -> a 2×2 grid filled row-major (TL, TR, BL, BR), first `count`
//                 cells; `gap` separates the cells (subtracted from each axis).
// count <= 0 returns an empty vector; count > 4 is clamped to 4.
[[nodiscard]] std::vector<SDL_FRect> cover_montage_rects(const SDL_FRect& box,
                                                         int   count,
                                                         float gap = 4.0f) noexcept;

}  // namespace ui
