#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <unordered_set>
#include <vector>

#include "image/decode_worker.h"
#include "ui/nav_model.h"
#include "ui/result_grid.h"
#include "ui/screen.h"
#include "ui/widgets.h"
#include "vault/vault.h"  // vault::SearchHit

namespace gfx { class Window; class FontAtlas; class Renderer; class TextureCache; }
namespace vault { class Vault; }

namespace ui {

// SearchResultView encapsulates the result-grid display and navigation logic,
// extracted from AdvancedSearchScreen (Phase 20) to manage the thumbnail grid view
// (Ctrl+L toggle List <-> Grid), result-item navigation (Up/Down/Left/Right), and
// off-thread thumbnail decoding.
//
// The view takes references to its dependencies (vault, window, font, cache) and
// owns the results list, the current selection cursor, and grid-specific state
// (view mode toggle, column count, decode worker, failed-set).
//
// Owned by AdvancedSearchScreen; initialized with dependency injection, and
// called during update() (pump_thumbnails), render() (render), and event dispatch
// (handle_key, activate_focused).
class SearchResultView {
public:
    SearchResultView(vault::Vault& vault, gfx::Window& win, gfx::FontAtlas& font,
                     gfx::TextureCache& cache);

    // Update step: upload any off-thread decode results to the texture cache.
    // Called by AdvancedSearchScreen::update() every frame.
    void pump_thumbnails();

    // Render the result view (list or grid, depending on grid_.view toggle).
    // Called by AdvancedSearchScreen::render_results().
    // hot = whether the Results field is focused and not in save mode or clearing mode
    void render(gfx::Renderer& r, float x, float colw, bool hot);

    // Navigate and activate results. Called by AdvancedSearchScreen::dispatch_focus_key()
    // when focus_ == Results.
    void handle_key(const SDL_KeyboardEvent& key);

    // Activate (open) the currently focused result. Called by handle_key() on Enter.
    // Returns a navigation request (screen change) to be processed by the parent.
    // For now, delegates to the parent Screen's request() method via a callback
    // (to be wired up by AdvancedSearchScreen).
    void activate_focused();

    // Session-state accessors: cursor position and view mode (List/Grid).
    // AdvancedSearchScreen uses these in on_enter/on_exit to restore/persist
    // the view state across screen visits.
    [[nodiscard]] int get_cursor() const { return cur_result_; }
    void set_cursor(int cursor) { cur_result_ = cursor; }

    [[nodiscard]] ResultView get_view() const { return grid_view_; }
    void set_view(ResultView view) { grid_view_ = view; }

    // Accessor to check the last-rendered column count (drives Up/Down stride in
    // Grid mode). Written by render(), read by handle_key().
    [[nodiscard]] int get_cols() const { return grid_cols_; }

    // Public data owned by this view: the results list. Mutated externally by
    // AdvancedSearchScreen::rerun() (the search is owned there), but navigated and
    // rendered by this view. Must be updated via update_results() to clamp the
    // cursor to the new list bounds.
    std::vector<vault::SearchHit> results;

    // Update the results list and clamp the cursor to the new size.
    // Called by AdvancedSearchScreen::rerun() after search evaluation.
    void update_results(const std::vector<vault::SearchHit>& new_results);

    // Callback for the parent Screen to provide a request(NavKind, path, idx) handler.
    // SearchResultView::activate_focused() calls this to navigate to the opened result.
    using RequestCallback = std::function<void(int, const std::string&, int)>;
    void set_request_callback(RequestCallback cb) { request_cb_ = std::move(cb); }

private:
    vault::Vault&       vault_;
    gfx::Window&        win_;
    gfx::FontAtlas&     font_;
    gfx::TextureCache&  cache_;

    int cur_result_ = 0;      // selected result index
    ResultView grid_view_ = ResultView::List;  // List or Grid toggle (Ctrl+L)
    int grid_cols_ = 1;      // last-rendered column count (drives Up/Down stride)

    image::DecodeWorker              grid_worker_{image::decode_wake_event()};
    std::unordered_set<uint64_t>     grid_failed_;  // failed thumbnail chunks

    RequestCallback request_cb_;  // callback to parent Screen for navigation
};

} // namespace ui
