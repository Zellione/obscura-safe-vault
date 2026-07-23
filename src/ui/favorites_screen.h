#pragma once

#include <SDL3/SDL.h>

#include <vector>

#include "ui/detail_model.h"
#include "ui/detail_panel.h"
#include "ui/nav_model.h"
#include "ui/quick_switch.h"
#include "ui/rename_dialog.h"
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
    void on_vault_changed() override;  // Phase 50: re-fetch favorites after tree reallocation
    void handle_event(const SDL_Event& e) override;
    void update(double dt) override;
    void render(gfx::Renderer& r) override;

    // Seed the panel's open state from the session. Set right after construction
    // rather than through the constructor, which would push the tag-screen
    // subclasses past the cpp:S107 parameter limit.
    void set_detail_open(bool open) { detail_.panel.open = open; }

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
    // Extra shortcut entries a subclass wants appended to the base "Navigate"
    // group in help_groups() (e.g. TagGalleries/TagImages' Tab toggle).
    [[nodiscard]] virtual std::vector<ui::HelpEntry> extra_help_entries() const { return {}; }
    [[nodiscard]] virtual bool show_favorite_badge() const { return true; }

    [[nodiscard]] std::vector<ui::HelpGroup> help_groups() const override;

    void reload();   // re-fetch favs_ + reset selection + seed cols_

    // Accessors for the two collaborators subclasses need (the data members stay
    // private — see S3656). Named with a `_ref` suffix so `vault_ref()` doesn't
    // shadow the `vault` namespace inside the class.
    [[nodiscard]] gfx::FontAtlas& font_ref()  const noexcept { return font_; }
    [[nodiscard]] vault::Vault&   vault_ref() const noexcept { return vault_; }

private:
    void open_selected();
    [[nodiscard]] int hit_test(float mx, float my) const;
    void start_rename();   // R: rename the focused item (Phase 45 Part 1)
    void rebuild_detail();

    gfx::Window&    win_;
    gfx::FontAtlas& font_;
    vault::Vault&   vault_;
    QuickSwitch     quick_switch_;   // ` overlay: jump to another vault
    RenameDialog    rename_;         // Phase 45 Part 1
    NavModel        nav_;   // selection only (no path stack used here)
    std::vector<vault::SearchHit> favs_;
    int             cols_ = 1;
    float           scroll_ = 0.0f;  // vertical scroll offset (pixels scrolled down)
    std::string     status_;         // transient feedback line (e.g. rename result)

    // Phase 48 detail panel. Bundled into a single member to stay under the
    // cpp:S1820 data-member cap. `key` is the cache key: rebuilding walks a
    // gallery subtree, so it happens only when the focused node actually changes.
    struct DetailState {
        DetailPanelState panel;
        DetailContent    content;
        std::string      key;
        float            content_h = 0.0f;
    };
    DetailState detail_;

    friend bool current_detail_open(const FavoritesScreen& s);
};

// App reads this to carry the panel's open state across screen transitions.
[[nodiscard]] bool current_detail_open(const FavoritesScreen& s);

} // namespace ui
