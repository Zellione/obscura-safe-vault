#pragma once

namespace ui {

// GalleryGrid's presentation mode: a flat metadata list, or one of four tiled
// grid densities. Session-scoped (Phase 39 Part 2 — GallerySessionState
// carries the last-used value across a grid<->viewer round trip); defaults
// to GridM, whose tile size matches the pre-Phase-40-Part-3 fixed grid
// exactly, so existing sessions render identically until a user opts into a
// different density.
enum class GalleryView { List, GridS, GridM, GridL, GridXL };

// Tile side (px) for a grid density; meaningless for List. GridM is 188 —
// the size every gallery grid used before this density cycle existed.
[[nodiscard]] float cell_size_for(GalleryView view) noexcept;

// L-key cycle order: List -> GridS -> GridM -> GridL -> GridXL -> List.
[[nodiscard]] GalleryView next_gallery_view(GalleryView view) noexcept;

} // namespace ui
