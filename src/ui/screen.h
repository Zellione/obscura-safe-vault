#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <utility>
#include <vector>

#include "ui/help_popup.h"

namespace gfx { class Renderer; }

namespace ui {

enum class NavKind {
    None, ToUnlock, ToGallery, ToViewer, ToFavoriteImages, ToFavoriteGalleries,
    ToFavoriteViewer, ToAdvancedSearch, ToTagOverview, ToTagGalleries,
    ToTagImages, ToTagViewer, ToImportStatus,
    ToVaultManager, LockActive, ToggleKeepUnlocked, ToSettings, Quit
};

// A transition request. `path`/`index` carry context for the destination:
//   ToGallery — reopen the grid at `path` with `index` selected (used when the
//               viewer returns to the leaf gallery it was launched from).
//   ToViewer  — open the viewer for the leaf gallery `path`, image `index`.
//   ToTagGalleries — `path` carries the tag whose galleries to list (Phase 22).
//   ToTagImages    — `path` carries the tag whose images/videos to list.
//   ToTagViewer    — viewer over a tag's media set; `path` = tag, `index` = pos.
//   ToSettings — open the global settings overlay on its Appearance section
//                (Phase 49; the C shortcut that used to open the theme picker).
struct Nav {
    NavKind     kind = NavKind::None;
    std::string path;
    int         index = 0;
};

// One full-window screen. App owns exactly one active screen, forwards raw SDL
// events to it, and consumes its transition request each frame via take_nav().
class Screen {
public:
    virtual ~Screen() = default;

    // Lifecycle hooks. Default to no-ops; screens override to acquire/release
    // resources (e.g. start text input, refresh listings) on activation.
    virtual void on_enter() { /* no-op by default */ }
    virtual void on_exit()  { /* no-op by default */ }

    // Phase 50: the vault's index tree changed under this screen (background
    // import drain attached nodes — push_child may have REALLOCATED children
    // vectors, invalidating every IndexNode* the screen holds). Screens that
    // cache node pointers across frames MUST override and re-fetch. Called on
    // the main thread, after records are applied, BEFORE the next render.
    virtual void on_vault_changed() { /* screens without cached pointers: no-op */ }

    virtual void handle_event(const SDL_Event& e) = 0;
    virtual void update(double dt) { (void)dt; }
    virtual void render(gfx::Renderer& r) = 0;

    // True while the screen is actively animating (e.g. a slideshow cross-fade)
    // and needs continuous redraws. Static screens leave this false so the app
    // loop can block on events and idle without spinning the GPU.
    [[nodiscard]] virtual bool animating() const { return false; }

    // True while the screen is doing work that must not be interrupted by the
    // idle auto-lock — e.g. a background import writing the vault on a worker
    // thread. Locking then would wipe the master key mid-write and corrupt the
    // vault, so the app treats this as "not idle" and resets its lock timer.
    [[nodiscard]] virtual bool blocks_idle_lock() const { return false; }

    // Grouped keyboard shortcuts for the F1 help popup. Default empty (safe
    // for any screen not yet updated). Screens may read their own current
    // state here (e.g. ImageViewer varies its video/image group by mode).
    [[nodiscard]] virtual std::vector<HelpGroup> help_groups() const { return {}; }

    // Redraw request. Screens call mark_dirty() when something changed outside
    // of input handling — typically an async result (file dialog, decode worker)
    // picked up in update(). The app loop consumes it to decide whether to
    // repaint; input events trigger a repaint on their own.
    void mark_dirty() noexcept { dirty_ = true; }
    [[nodiscard]] bool consume_dirty() noexcept { const bool d = dirty_; dirty_ = false; return d; }

    [[nodiscard]] Nav take_nav() { Nav n = std::move(nav_); nav_ = {}; return n; }

protected:
    void request(NavKind k) { nav_ = Nav{k, {}, 0}; }
    void request(NavKind k, std::string path, int index = 0)
    {
        nav_ = Nav{k, std::move(path), index};
    }

private:
    Nav  nav_{};
    bool dirty_ = true;   // draw the first frame after activation
};

} // namespace ui
