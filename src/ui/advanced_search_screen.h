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

    // The in-progress text buffers + small builder state (grouped to keep the
    // screen's field count modest).
    struct Edit {
        std::string name;
        std::string include;
        std::string exclude;
        std::string group;
        int         weight = 1;   // weight applied to the next include tag
    };

    // Selection cursors for the lists/dropdowns.
    struct Cursor {
        int result = 0;
        int saved  = 0;
        int sugg   = -1;   // highlighted suggestion (-1 = none)
        int group  = 0;    // current group for the Group field
    };

    // --- data flow ---
    void rerun();             // re-evaluate query_ → results_
    void reload_saved();      // refresh saved_ + vocabulary_ from the vault
    void refresh_suggestions();

    // --- event handling (split into small per-focus handlers) ---
    void handle_text(const char* text);
    void handle_key(const SDL_KeyboardEvent& key);
    void dispatch_focus_key(const SDL_KeyboardEvent& key);
    void handle_save_key(const SDL_KeyboardEvent& key);
    void handle_axis_key(const SDL_KeyboardEvent& key);   // Left/Right on Scope or GroupJoin
    void handle_tag_field_key(const SDL_KeyboardEvent& key);
    void handle_group_nav_key(const SDL_KeyboardEvent& key);
    void handle_weight_key(const SDL_KeyboardEvent& key);
    void handle_results_key(const SDL_KeyboardEvent& key);
    void handle_saved_key(const SDL_KeyboardEvent& key);

    void cycle_focus(int dir);
    void cycle_scope(int dir);
    void move_suggestion(int dir);
    void commit_text();       // Enter on the focused text field
    void commit_group_text(std::string tag);
    void backspace();         // Backspace on the focused text field
    void open_result();       // activate the focused result
    void load_saved();        // load the focused saved search into query_
    void delete_saved();      // delete the focused saved search
    void begin_save();        // start naming a new saved search
    void confirm_save();      // finish naming → Vault::save_search

    [[nodiscard]] std::string* active_buffer();
    [[nodiscard]] std::string accepted(const std::string& buf) const;  // suggestion-or-typed

    // --- rendering (split per column) ---
    void render_builder(gfx::Renderer& r, float x, float top);
    void render_results(gfx::Renderer& r, float x, float colw);
    void render_saved(gfx::Renderer& r, float x);

    gfx::Window&    win_;
    gfx::FontAtlas& font_;
    vault::Vault&   vault_;

    AdvancedQuery                   query_;
    std::vector<vault::SearchHit>   results_;
    std::vector<vault::SavedSearch> saved_;
    std::vector<std::string>        vocabulary_;   // distinct vault tags (autocomplete)
    std::vector<std::string>        suggestions_;  // current typeahead list

    Focus  focus_ = Focus::Include;
    Edit   edit_;
    Cursor cur_;

    bool        saving_ = false;   // naming a search to save
    std::string save_buf_;
    std::string status_;           // transient feedback line
};

} // namespace ui
