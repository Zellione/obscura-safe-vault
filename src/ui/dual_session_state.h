#pragma once

#include <array>
#include <string>
#include <vector>

namespace ui {

// State for one pane of the dual-pane gallery (Phase 78). `selected_tiles`
// are listing indices — clamped/dropped on restore if the listing changed.
// The pane's view/density is NOT stored: it is the shared machine-wide
// setting (Phase 93), so both panes — like every surface — follow it.
struct PaneState {
    std::string      path;                 // gallery slash-path, "" = root
    int              selected = 0;         // focused tile index
    float scroll = 0.0f;                   // pane scroll offset
    bool             detail_open = false;
    std::vector<int> selected_tiles;       // multi-selection
};

// Everything needed to rebuild both panes exactly (Phase 78).
struct DualSessionState {
    bool      split_active = false;  // true while user is conceptually in split view (set on enter, false on F3-leave)
    bool      has_config   = false;  // true if pane configs have been saved (set by snapshot, cleared on reset)
    int       active_pane  = 0;      // 0 = left, 1 = right
    std::array<PaneState, 2> pane;
    void reset() { *this = DualSessionState{}; }  // clears split_active and has_config
};

} // namespace ui
