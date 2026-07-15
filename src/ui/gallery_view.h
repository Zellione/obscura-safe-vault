#pragma once

namespace ui {

// GalleryGrid's presentation mode: tiled thumbnails or a flat metadata list.
// Session-scoped (Phase 39 Part 2 — GallerySessionState carries the last-used
// value across a grid<->viewer round trip); defaults to Grid.
enum class GalleryView { Grid, List };

} // namespace ui
