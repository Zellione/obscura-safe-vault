#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "image/decode_worker.h"
#include "ui/advanced_search_model.h"
#include "ui/result_grid.h"
#include "ui/screen.h"
#include "vault/vault.h"          // vault::SearchHit, vault::SavedSearch
#include "vault/vault_search.h"   // vault::VaultSearch facade

namespace gfx { class Window; class FontAtlas; class Renderer; class TextureCache; }
namespace vault { class Vault; }

namespace ui {

// The Phase 18 advanced-search screen: a first-class Screen (opened with Shift+/
// from the gallery grid) hosting a keyboard-driven query builder, a live result
// list, and a saved-searches sidebar. The Phase 12 `/` quick-overlay is separate
// and unchanged.
//
// All matching/ranking lives in the pure `advanced_search_model` + the
// `VaultSearch` facade; this screen is only SDL plumbing: it edits an
// AdvancedQuery, re-runs the search on every change, and renders the columns.
class AdvancedSearchScreen : public Screen {
public:
    AdvancedSearchScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                         gfx::TextureCache& cache);

    void on_enter() override;
    void on_exit() override;
    void handle_event(const SDL_Event& e) override;
    void update(double dt) override;   // upload finished off-thread thumb decodes
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
        int tag    = -1;   // selected committed tag in the focused field (-1 = none/editing)
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

    // Committed-tag selection within the focused tag field (Include/Exclude/Group).
    // Kept as free functions (friends) so they don't count against the class method
    // budget (cpp:S1448), mirroring the VaultSearch facade pattern. They mutate the
    // private query/cursor/edit state, hence the friendship.
    friend int  current_tag_count(const AdvancedSearchScreen& s);  // # tags in focused field
    friend void select_tag(AdvancedSearchScreen& s, int dir);      // move cur_.tag
    friend void remove_selected_tag(AdvancedSearchScreen& s);      // erase selected tag, rerun
    friend void edit_selected_tag(AdvancedSearchScreen& s);        // pull selected tag into buffer

    // Phase 20 grid result-view plumbing. Free friends (same S1448 rationale as
    // the tag helpers above): the thumbnail-grid result renderer and its
    // off-thread-decode pump, both needing the private results/cursor/cache/worker.
    friend void pump_search_thumbs(AdvancedSearchScreen& s);
    friend void render_result_grid(AdvancedSearchScreen& s, gfx::Renderer& r,
                                   float x, float colw);

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
    void render_builder(gfx::Renderer& r, float x, float top, float colw);
    void render_results(gfx::Renderer& r, float x, float colw);
    void render_saved(gfx::Renderer& r, float x);

    gfx::Window&       win_;
    gfx::FontAtlas&    font_;
    vault::Vault&      vault_;
    gfx::TextureCache& cache_;    // shared GPU thumbnail cache (grid result view)
    vault::VaultSearch search_;   // advanced-search facade over vault_

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

    // Phase 20: result-panel presentation (session-scoped). Ctrl+L toggles
    // List <-> Grid; the grid view reuses the gallery's tile-thumbnail draw, so
    // it needs its own off-thread decode worker + failed-set + column count.
    ResultView  result_view_ = ResultView::List;
    int         result_cols_ = 1;   // last-rendered grid column count (for nav)
    struct ThumbDecode {
        image::DecodeWorker          worker{image::decode_wake_event()};
        std::unordered_set<uint64_t> failed;
    };
    ThumbDecode thumbs_;
};

} // namespace ui
