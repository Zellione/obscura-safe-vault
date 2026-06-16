#pragma once

#include <SDL3/SDL.h>

#include <vector>

#include "ui/favorites_screen.h"
#include "vault/vault.h"   // vault::SearchHit

namespace gfx { class Window; class FontAtlas; class Renderer; }
namespace vault { class Vault; }

namespace ui {

// A flat grid of every favorited *gallery* across the whole vault (Phase 13).
// Activating one navigates to that gallery in the normal grid. Reached from the
// gallery grid with Shift+F; Esc/Backspace returns to the root gallery.
class FavoritesGalleries : public FavoritesScreen {
public:
    using FavoritesScreen::FavoritesScreen;

protected:
    [[nodiscard]] std::vector<vault::SearchHit> fetch() const override;
    void draw_tile_content(gfx::Renderer& r, const vault::SearchHit& hit,
                           const SDL_FRect& box) override;
    void activate(const vault::SearchHit& hit) override;
    [[nodiscard]] const char* title() const override { return "Favorite Galleries"; }
    [[nodiscard]] const char* empty_hint() const override
    {
        return "No favorite galleries yet. Press [B] on a gallery tile to bookmark it.";
    }
};

} // namespace ui
