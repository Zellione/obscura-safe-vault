#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

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
    GalleryView view                 = GalleryView::GridM;   // last-used, session-global
    StripSide   strip_side           = StripSide::Bottom;    // last-used, session-global
    bool        detail_open          = false;                // detail panel toggle, session-global

    // Resume bookmark for the single most-recently-left video (paused, not
    // autoplaying); empty/zero means "nothing to resume". Overwritten each
    // time a different video is left; a plain image clears it.
    std::string last_media_path;
    double      video_resume_seconds = 0.0;

    // Phase 40 Part 2: last-focused tile index at every gallery path visited
    // this session, so descending/backing-out/leaving-and-returning restores
    // it instead of always landing on tile 0. Key = NavModel::path() ("a/b/c",
    // "" for root). GalleryGrid writes into this directly (unlike view/strip-
    // side/video-resume above, which App captures only once at screen exit)
    // because a single GalleryGrid instance can descend through many
    // sub-galleries without ever being destroyed.
    std::unordered_map<std::string, int> last_index_by_path;

    void record(std::string_view path, int index) { last_index_by_path[std::string(path)] = index; }

    [[nodiscard]] int recall(std::string_view path) const
    {
        const auto it = last_index_by_path.find(std::string(path));
        return it == last_index_by_path.end() ? 0 : it->second;
    }

    void reset() { *this = GallerySessionState{}; }
};

} // namespace ui
