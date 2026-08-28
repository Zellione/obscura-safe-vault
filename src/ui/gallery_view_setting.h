#pragma once

#include "ui/gallery_view.h"

namespace ui {

// Process-global gallery view (Phase 93). Seeded from platform::GalleryViewPref
// at App::init and in promote_pending, written back (and persisted) by every
// surface's `L` key and the F2 settings "Default Gallery View" row. This is the
// single in-memory source of truth for the shared density: the gallery grid,
// favorites/tag screens, and advanced-search results all read it on entry and
// write it on change, so changing the density anywhere affects everywhere.
// UI-thread only (like the active-theme / autoplay / clipboard globals); no
// synchronisation needed. Defaults to GridM until seeded.
[[nodiscard]] GalleryView gallery_view_setting() noexcept;
void set_gallery_view_setting(GalleryView view) noexcept;

}  // namespace ui