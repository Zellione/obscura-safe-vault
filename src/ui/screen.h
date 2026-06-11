#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <utility>

namespace gfx { class Renderer; }

namespace ui {

enum class NavKind { None, ToUnlock, ToGallery, ToViewer, Quit };

// A transition request. `path`/`index` carry context for the destination:
//   ToGallery — reopen the grid at `path` with `index` selected (used when the
//               viewer returns to the leaf gallery it was launched from).
//   ToViewer  — open the viewer for the leaf gallery `path`, image `index`.
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

    virtual void handle_event(const SDL_Event& e) = 0;
    virtual void update(double dt) { (void)dt; }
    virtual void render(gfx::Renderer& r) = 0;

    [[nodiscard]] Nav take_nav() { Nav n = std::move(nav_); nav_ = {}; return n; }

protected:
    void request(NavKind k) { nav_ = Nav{k, {}, 0}; }
    void request(NavKind k, std::string path, int index = 0)
    {
        nav_ = Nav{k, std::move(path), index};
    }

private:
    Nav nav_{};
};

} // namespace ui
