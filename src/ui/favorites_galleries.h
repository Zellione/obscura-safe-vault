#pragma once

#include <SDL3/SDL.h>

#include <vector>

#include "ui/nav_model.h"
#include "ui/screen.h"
#include "vault/vault.h"   // vault::SearchHit

namespace gfx { class Window; class FontAtlas; class Renderer; }
namespace vault { class Vault; }

namespace ui {

// A flat grid of every favorited *gallery* across the whole vault (Phase 13).
// Activating one navigates to that gallery in the normal grid. Reached from the
// gallery grid with Shift+F; Esc/Backspace returns to the root gallery.
class FavoritesGalleries : public Screen {
public:
    FavoritesGalleries(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault);

    void on_enter() override;
    void handle_event(const SDL_Event& e) override;
    void render(gfx::Renderer& r) override;

private:
    void open_selected();
    [[nodiscard]] int hit_test(float mx, float my) const;

    gfx::Window&    win_;
    gfx::FontAtlas& font_;
    vault::Vault&   vault_;
    NavModel        nav_;   // selection only (no path stack used here)
    std::vector<vault::SearchHit> favs_;
    int             cols_ = 1;
};

} // namespace ui
