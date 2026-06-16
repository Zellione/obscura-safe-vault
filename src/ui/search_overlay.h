#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "ui/screen.h"
#include "vault/vault.h"

namespace gfx { class Renderer; class FontAtlas; class Window; }
namespace vault { class Vault; struct SearchHit; }

namespace ui {

// A modal search panel overlaying the gallery grid (Phase 12). Handles text input
// (live filtering), scope toggling (Images / Galleries / Both), and returns a
// navigation request when a result is selected.
//
// Usage:
//   1. open() to activate the modal
//   2. Call handle_event() from the grid's event loop
//   3. Each frame, check active() to know if the modal is up
//   4. Call render() to draw the overlay
//   5. Call take_nav() to poll for a navigation request
class SearchOverlay {
public:
    SearchOverlay(vault::Vault& vault, gfx::Window& win) : vault_(vault), win_(win) {}

    void open();           // activate the search modal
    void close();          // deactivate
    [[nodiscard]] bool active() const noexcept { return active_; }

    // Handle text input and keyboard events while the modal is open.
    // Returns true if the event was consumed (caller should not process it).
    [[nodiscard]] bool handle_event(const SDL_Event& e);

    // Poll for a navigation result. Returns a non-None nav if a result was selected.
    // The nav is returned exactly once; subsequent calls return None.
    [[nodiscard]] Nav take_nav() { Nav n = std::move(nav_); nav_ = {}; return n; }

    void render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H);

private:
    void refresh_results();        // rebuild the result list based on query_ and scope_
    void cycle_scope();            // Both -> Images -> Galleries -> Both
    void move_selection(int delta); // clamp the highlighted row
    void activate_selected();      // turn the highlighted result into a nav request
    [[nodiscard]] Nav nav_for_image(const vault::SearchHit& hit) const;

    vault::Vault&                  vault_;
    gfx::Window&                   win_;
    bool                           active_ = false;
    std::string                    query_;
    vault::SearchScope             scope_ = vault::SearchScope::Both;
    std::vector<vault::SearchHit>  all_results_;   // all matches in current scope
    std::vector<const vault::SearchHit*> filtered_;  // live-filtered + ranked
    int                            selected_ = 0;    // index in filtered_
    Nav                            nav_{};
};

} // namespace ui
