#pragma once

#include <SDL3/SDL.h>

#include <vector>

#include "ui/nav_model.h"
#include "ui/quick_switch.h"
#include "ui/screen.h"
#include "vault/vault.h"   // vault::SearchHit

namespace gfx { class Window; class FontAtlas; class Renderer; }
namespace vault { class Vault; }
namespace platform { class VaultRegistry; }

namespace ui {

// Shared scaffolding for the two Phase 13 favorites screens: a flat grid of
// favorited nodes (`SearchHit`s) with keyboard/mouse selection, a header, and a
// gold favorite badge on every tile. Subclasses supply the item list, the
// per-tile content (thumbnail vs folder glyph), the activation action, and the
// labels — this base owns only the common SDL plumbing + grid layout.
class FavoritesScreen : public Screen {
public:
    FavoritesScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                    platform::VaultRegistry& registry, std::string active_path);

    void on_enter() override;
    void handle_event(const SDL_Event& e) override;
    void render(gfx::Renderer& r) override;

protected:
    // Subclass hooks.
    [[nodiscard]] virtual std::vector<vault::SearchHit> fetch() const = 0;
    virtual void draw_tile_content(gfx::Renderer& r, const vault::SearchHit& hit,
                                   const SDL_FRect& box) = 0;
    // Open the focused favorite. `index` is its position in the favorites list
    // (used by the images screen to open the viewer over the whole set).
    virtual void activate(const vault::SearchHit& hit, int index) = 0;
    [[nodiscard]] virtual const char* title() const = 0;
    [[nodiscard]] virtual const char* empty_hint() const = 0;

    // Where Back (Esc/Backspace) navigates. Defaults to the root gallery (the
    // favorites screens' behaviour); the Phase 22 tag-galleries view overrides it
    // to return to the tag overview.
    virtual void go_back() { request(NavKind::ToGallery, "", 0); }

    // Extra subclass input + chrome hooks (default no-ops; the favorites screens
    // don't use them). The tag views use them for the Galleries⟷Images Tab
    // toggle, an extra hint, and to suppress the favorite badge (their tiles are
    // tag matches, not favorites).
    virtual bool handle_extra_key(const SDL_KeyboardEvent& /*key*/) { return false; }
    [[nodiscard]] virtual const char* extra_hint() const { return ""; }
    [[nodiscard]] virtual bool        show_favorite_badge() const { return true; }

    void reload();   // re-fetch favs_ + reset selection + seed cols_

    // Accessors for the two collaborators subclasses need (the data members stay
    // private — see S3656). Named with a `_ref` suffix so `vault_ref()` doesn't
    // shadow the `vault` namespace inside the class.
    [[nodiscard]] gfx::FontAtlas& font_ref()  const noexcept { return font_; }
    [[nodiscard]] vault::Vault&   vault_ref() const noexcept { return vault_; }

private:
    void open_selected();
    [[nodiscard]] int hit_test(float mx, float my) const;

    gfx::Window&    win_;
    gfx::FontAtlas& font_;
    vault::Vault&   vault_;
    QuickSwitch     quick_switch_;   // ` overlay: jump to another vault
    NavModel        nav_;   // selection only (no path stack used here)
    std::vector<vault::SearchHit> favs_;
    int             cols_ = 1;
};

} // namespace ui
