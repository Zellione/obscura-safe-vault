#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "ui/advanced_search_model.h"
#include "ui/screen.h"
#include "vault/vault.h"   // vault::SearchHit, vault::SavedSearch

namespace gfx { class Window; class FontAtlas; class Renderer; }
namespace vault { class Vault; }

namespace ui {

// The Phase 18 advanced-search screen: a first-class Screen (opened with Shift+/
// from the gallery grid) hosting a keyboard-driven query builder, a live result
// list, and a saved-searches sidebar. The Phase 12 `/` quick-overlay is separate
// and unchanged.
//
// All matching/ranking lives in the pure `advanced_search_model` + `Vault`; this
// screen is only SDL plumbing: it edits an AdvancedQuery, re-runs the search on
// every change, and renders the three columns.
class AdvancedSearchScreen : public Screen {
public:
    AdvancedSearchScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault);

    void on_enter() override;
    void on_exit() override;
    void handle_event(const SDL_Event& e) override;
    void render(gfx::Renderer& r) override;

private:
    // Focusable fields, cycled with Tab / Shift+Tab.
    enum class Focus { Name, Scope, Include, Exclude, Group, GroupJoin, Results, Saved };

    void rerun();                       // re-evaluate query_ → results_
    void reload_saved();                // refresh saved_ + vocabulary_ from the vault
    void cycle_focus(int dir);
    void handle_key(const SDL_KeyboardEvent& key);
    void handle_text(const char* text);
    void commit_text();                 // Enter on the focused text field
    void backspace();                   // Backspace on the focused text field
    void open_result();                 // activate the focused result
    void load_saved();                  // load the focused saved search into query_
    void delete_saved();                // delete the focused saved search
    void begin_save();                  // start naming a new saved search
    void confirm_save();                // finish naming → Vault::save_search
    [[nodiscard]] std::string* active_buffer();  // the text buffer for the current focus
    void refresh_suggestions();

    gfx::Window&        win_;
    gfx::FontAtlas&     font_;
    vault::Vault&       vault_;

    AdvancedQuery       query_;
    std::vector<vault::SearchHit>  results_;
    std::vector<vault::SavedSearch> saved_;
    std::vector<std::string>       vocabulary_;     // distinct vault tags (autocomplete)
    std::vector<std::string>       suggestions_;    // current typeahead list

    Focus  focus_      = Focus::Include;
    int    scope_idx_  = 2;     // 0=Images 1=Galleries 2=Both
    int    group_sel_  = 0;     // current group index for the Group field
    int    result_sel_ = 0;
    int    saved_sel_  = 0;
    int    sugg_sel_   = 0;     // highlighted suggestion (-1 = none)

    std::string name_buf_;      // text buffers per text field
    std::string inc_buf_;
    std::string exc_buf_;
    std::string grp_buf_;
    int         inc_weight_ = 1;  // weight applied to the next include tag

    bool        saving_     = false;   // naming a search to save
    std::string save_buf_;
    std::string status_;               // transient feedback line
};

} // namespace ui
