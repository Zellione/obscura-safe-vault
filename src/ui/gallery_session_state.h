#pragma once

#include <string>

#include "ui/gallery_view.h"   // ui::GalleryView
#include "ui/strip_layout.h"   // ui::StripSide

namespace ui {

// Session-scoped gallery/viewer state, preserved across grid<->viewer round
// trips within a single unlocked-vault session (Phase 39 Part 2, modeled on
// AdvancedSearchState). App owns one instance, reads the outgoing screen's
// current view/strip-side/video-position into it just before destroying that
// screen on every ToGallery/ToViewer transition, and feeds the relevant
// fields into the new screen's constructor instead of always defaulting.
//
// Deliberately does NOT duplicate the gallery path/focused-index the grid was
// on — Nav{kind, path, index} (screen.h) already carries that. Reset to
// defaults at the same points AdvancedSearchState resets at: explicit lock,
// idle auto-lock, and vault switch.
struct GallerySessionState {
    GalleryView view                 = GalleryView::Grid;   // last-used, session-global
    StripSide   strip_side           = StripSide::Bottom;    // last-used, session-global

    // Resume bookmark for the single most-recently-left video (paused, not
    // autoplaying); empty/zero means "nothing to resume". Overwritten each
    // time a different video is left; a plain image clears it.
    std::string last_media_path;
    double      video_resume_seconds = 0.0;

    void reset() { *this = GallerySessionState{}; }
};

} // namespace ui
