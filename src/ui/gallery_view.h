#pragma once

#include <string_view>

namespace ui {

// GalleryGrid's presentation mode: a flat metadata list, or one of four tiled
// grid densities. Session-scoped (Phase 39 Part 2 — GallerySessionState
// carries the last-used value across a grid<->viewer round trip); defaults
// to GridM, whose tile size matches the pre-Phase-40-Part-3 fixed grid
// exactly, so existing sessions render identically until a user opts into a
// different density.
enum class GalleryView { List, GridS, GridM, GridL, GridXL };

// Tile side (px) for a grid density; meaningless for List. GridM is 256
// (Phase 75 bump; was 188).
[[nodiscard]] float cell_size_for(GalleryView view) noexcept;

// L-key cycle order: List -> GridS -> GridM -> GridL -> GridXL -> List.
[[nodiscard]] GalleryView next_gallery_view(GalleryView view) noexcept;

// UI display label for a gallery view ("List", "Grid S", etc.), used in
// footer/settings rows.
[[nodiscard]] std::string_view gallery_view_label(GalleryView view) noexcept;

// Inverse of next_gallery_view: cycles backwards through the same sequence.
[[nodiscard]] GalleryView prev_gallery_view(GalleryView view) noexcept;

// Configuration token for a gallery view ("list", "grid-s", etc.); stable
// and never renamed to preserve persisted gallery_view.conf entries.
[[nodiscard]] std::string_view gallery_view_slug(GalleryView view) noexcept;

// Parse a slug string to a gallery view; unknown or empty slug defaults to
// GridM.
[[nodiscard]] GalleryView gallery_view_from_slug(std::string_view slug) noexcept;

} // namespace ui
