#pragma once

#include <SDL3/SDL.h>

#include <vector>

#include "ui/collection_ops.h"
#include "ui/detail_model.h"
#include "ui/detail_panel.h"
#include "ui/gallery_view.h"
#include "ui/nav_model.h"
#include "ui/quick_switch.h"
#include "ui/rename_dialog.h"
#include "ui/screen.h"
#include "ui/selection_model.h"
#include "vault/vault.h"   // vault::SearchHit

namespace gfx { class Window; class FontAtlas; class Renderer; }
namespace vault { class Vault; }
namespace platform { class VaultRegistry; class FileDialog; class FolderDialog; }

namespace ui { class ImportQueue; class SecondVaultSession; }

namespace ui {

// Shared scaffolding for the two Phase 13 favorites screens: a flat grid of
// favorited nodes (`SearchHit`s) with keyboard/mouse selection, a header, and a
// gold favorite badge on every tile. Subclasses supply the item list, the
// per-tile content (thumbnail vs folder glyph), the activation action, and the
// labels — this base owns only the common SDL plumbing + grid layout.
class FavoritesScreen : public Screen {
public:
    // App-owned collaborators for the Phase 68 batch operations (B/X/M over the
    // Space/Ctrl+A selection). Bundled so the ctor stays under the S107 cap:
    // the file dialog feeds the destination-vault keyfile pick, the folder
    // dialog picks the export destination (App-owned — its async callback must
    // outlive this screen), the queue gates transfers (Phase 50 exclusivity),
    // and the warm second-vault slot is Phase 66's.
    struct CollectionOps {
        platform::FileDialog&   file_dialog;
        platform::FolderDialog& folder_dialog;
        ImportQueue&            queue;
        SecondVaultSession*     second;
        std::string             active_path;   // active vault's file path
    };

    FavoritesScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                    platform::VaultRegistry& registry, const CollectionOps& ops);

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

    // True while a batch worker (export job or transfer) owns the vault handle.
    // Subclasses MUST NOT touch the vault (no thumbnail decode submits) while
    // this holds — same contract as GalleryGrid's vault_busy.
    [[nodiscard]] bool batch_ops_busy() const noexcept { return ops_.busy(); }

private:
    void open_selected();
    // The SDL_EVENT_KEY_DOWN half of handle_event, split out to keep each
    // function under the S3776 cognitive-complexity cap.
    void handle_key_down(const SDL_KeyboardEvent& key);
    [[nodiscard]] int hit_test(float mx, float my) const;
    void start_rename();   // R: rename the focused item (Phase 45 Part 1)
    void rebuild_detail();

    // Phase 68 batch operations over the Space/Ctrl+A selection.
    void toggle_select_all();
    void toggle_favorite_batch();    // B
    void start_export();             // X: consent modal first
    void start_transfer();           // M: per-parent groups + gallery subtrees
    void start_delete_selection();   // Del: batch delete

    gfx::Window&    win_;
    gfx::FontAtlas& font_;
    vault::Vault&   vault_;
    QuickSwitch     quick_switch_;   // ` overlay: jump to another vault
    RenameDialog    rename_;         // Phase 45 Part 1
    NavModel        nav_;   // selection only (no path stack used here)
    std::vector<vault::SearchHit> favs_;
    int             cols_ = 1;
    // Phase 93: shared grid density (S/M/L/XL/XXL) for the tile grid. Seeded
    // from ui::gallery_view_setting() on entry; `L` cycles via
    // next_grid_density and writes the result back to the shared setting.
    GalleryView view_ = GalleryView::GridM;
    float           scroll_ = 0.0f;  // vertical scroll offset (pixels scrolled down)
    // One-shot scroll-follow request, applied then cleared by update(): set when
    // key navigation moves the selection. The mouse wheel scrolls WITHOUT moving
    // the selection, so following every frame would snap the view back (jitter,
    // and a hard wall at the selection's visibility window) — same contract as
    // GalleryGrid::ScrollFollow, minus the Center mode (entering always lands on
    // index 0 at scroll 0, so there is nothing to center).
    bool            follow_scroll_ = false;
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

    // Phase 68: the shared batch-operation flows (consent-gated export, grouped
    // move/copy) + the multi-selection they act on.
    CollectionBatchOps ops_;
    SelectionModel     sel_;

    friend bool current_detail_open(const FavoritesScreen& s);
    // Phase 68/93 single-letter selection shortcuts (Shift+I, L, Space, Ctrl+A,
    // B, X, M, Del). A free friend for the same cpp:S3776 reason as
    // gallery_grid_handle_shortcut_keys — keeps handle_key_down under the cap.
    friend bool favorites_handle_shortcut_keys(FavoritesScreen& s, const SDL_KeyboardEvent& key);
};

// App reads this to carry the panel's open state across screen transitions.
[[nodiscard]] bool current_detail_open(const FavoritesScreen& s);

} // namespace ui
