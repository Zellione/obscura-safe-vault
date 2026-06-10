#pragma once

#include <SDL3/SDL.h>

namespace gfx { class Renderer; }

namespace ui {

enum class NavKind { None, ToUnlock, ToGallery, Quit };

struct Nav { NavKind kind = NavKind::None; };

// One full-window screen. App owns exactly one active screen, forwards raw SDL
// events to it, and consumes its transition request each frame via take_nav().
class Screen {
public:
    virtual ~Screen() = default;

    virtual void on_enter() {}
    virtual void on_exit()  {}

    virtual void handle_event(const SDL_Event& e) = 0;
    virtual void update(double dt) { (void)dt; }
    virtual void render(gfx::Renderer& r) = 0;

    [[nodiscard]] Nav take_nav() noexcept { Nav n = nav_; nav_ = {}; return n; }

protected:
    void request(NavKind k) noexcept { nav_ = Nav{k}; }

private:
    Nav nav_{};
};

} // namespace ui
