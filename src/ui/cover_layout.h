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

// A folder backdrop for a gallery tile: a gold body plate with a small tab
// peeking up at the top-left, and an inset `inner` area where the cover montage
// is drawn so the folder colour frames the photo on all sides. This is what
// makes a gallery (folder of items) unmistakable from a plain image tile.
struct FolderFrame {
    SDL_FRect tab;    // small tab above the body, top-left (drawn first, behind)
    SDL_FRect body;   // folder body plate (full width, dropped by the tab height)
    SDL_FRect inner;  // inset cover area = body minus `frame` on every side
};

// `frame` is both the cover inset and the tab/body overlap (so no seam shows).
[[nodiscard]] FolderFrame folder_frame(const SDL_FRect& box,
                                       float frame = 5.0f) noexcept;

}  // namespace ui
