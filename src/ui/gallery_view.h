#pragma once

#include <string_view>

namespace ui {

// A presentation mode shared by the gallery grid AND the collection grids
// (favorites/tag screens, advanced-search results, Phase 93): a flat metadata
// list, or one of five tiled grid densities. Machine-scoped (Phase 84 +
// Phase 93 — the value lives in `gallery_view.conf` via GalleryViewPref and
// the ui::gallery_view_setting process-global, and every surface shares it);
// defaults to GridM, whose tile size matches the pre-Phase-40-Part-3 fixed
// grid exactly, so existing sessions render identically until a user opts into
// a different density.
enum class GalleryView { List, GridS, GridM, GridL, GridXL, GridXXL };

// Tile side (px) for a grid density; meaningless for List. GridM is 288
// (Phase 93 bump from 256, which was the Phase 75 bump from 188).
[[nodiscard]] float cell_size_for(GalleryView view) noexcept;

// L-key cycle order: List -> GridS -> GridM -> GridL -> GridXL -> GridXXL -> List.
[[nodiscard]] GalleryView next_gallery_view(GalleryView view) noexcept;

// Grid-density-only cycle for surfaces that have no List mode (favorites and
// the tag screens): GridS -> GridM -> GridL -> GridXL -> GridXXL -> GridS.
// An input of List is treated as GridS, so pressing L right after a surface
// read `List` from the shared setting lands on GridS rather than looping.
[[nodiscard]] GalleryView next_grid_density(GalleryView view) noexcept;

// Normalize a shared view for a grid-only surface, then return its tile size.
// List falls back to GridM because these surfaces have no list renderer.
[[nodiscard]] GalleryView grid_view_for(GalleryView view) noexcept;
[[nodiscard]] float grid_cell_size(GalleryView view) noexcept;

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
