#pragma once

#include <string>

#include "ui/advanced_search_model.h"   // ui::AdvancedQuery
#include "ui/result_grid.h"             // ui::ResultView

namespace ui {

// Session-scoped advanced-search state, preserved across visits to the
// AdvancedSearchScreen within a single unlocked-vault session (Phase 20
// follow-up). App owns one instance and passes it to the screen by reference;
// the screen restores from it in on_enter() and writes back in on_exit(), so
// returning to the screen shows the previous query, parameters, cursor and
// view mode. App resets it to a default (`active = false`) whenever the active
// vault changes (lock / switch / idle auto-lock).
//
// Results are deliberately NOT stored — they are re-derived by re-running
// `query` on entry (SearchHit::node pointers would otherwise dangle across a
// screen recreation). The flat builder buffers mirror the screen's private
// Edit/Cursor state so this struct stays free of the screen's nested types.
struct AdvancedSearchState {
    bool          active = false;   // false until the first save -> fresh defaults

    AdvancedQuery query;            // the committed query (include/exclude/groups/scope/name)

    // In-progress builder text buffers (mirror AdvancedSearchScreen::Edit).
    std::string   name;
    std::string   include;
    std::string   exclude;
    std::string   group;
    int           weight = 1;       // weight applied to the next include tag

    int           focus  = 0;       // AdvancedSearchScreen::Focus underlying value (set on save)
    int           cur_result = 0;
    int           cur_group  = 0;
    int           cur_saved  = 0;

    ResultView    view = ResultView::List;
};

} // namespace ui
