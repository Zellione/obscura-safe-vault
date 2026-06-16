#pragma once

#include <SDL3/SDL.h>

#include <vector>

#include "ui/nav_model.h"
#include "ui/screen.h"
#include "vault/vault.h"   // vault::SearchHit

namespace gfx { class Window; class FontAtlas; class Renderer; }
namespace vault { class Vault; }

namespace ui {

// Shared scaffolding for the two Phase 13 favorites screens: a flat grid of
// favorited nodes (`SearchHit`s) with keyboard/mouse selection, a header, and a
// gold favorite badge on every tile. Subclasses supply the item list, the
// per-tile content (thumbnail vs folder glyph), the activation action, and the
// labels — this base owns only the common SDL plumbing + grid layout.
class FavoritesScreen : public Screen {
public:
    FavoritesScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault);

    void on_enter() override;
    void handle_event(const SDL_Event& e) override;
    void render(gfx::Renderer& r) override;

protected:
    // Subclass hooks.
    [[nodiscard]] virtual std::vector<vault::SearchHit> fetch() const = 0;
    virtual void draw_tile_content(gfx::Renderer& r, const vault::SearchHit& hit,
                                   const SDL_FRect& box) = 0;
    virtual void activate(const vault::SearchHit& hit) = 0;
    [[nodiscard]] virtual const char* title() const = 0;
    [[nodiscard]] virtual const char* empty_hint() const = 0;

    void reload();   // re-fetch favs_ + reset selection + seed cols_

    gfx::Window&    win_;
    gfx::FontAtlas& font_;
    vault::Vault&   vault_;
    NavModel        nav_;   // selection only (no path stack used here)
    std::vector<vault::SearchHit> favs_;
    int             cols_ = 1;

private:
    void open_selected();
    [[nodiscard]] int hit_test(float mx, float my) const;
};

} // namespace ui
