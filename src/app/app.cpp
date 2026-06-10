#include "app.h"

#include <print>

#include "gfx/renderer.h"
#include "platform/paths.h"
#include "ui/gallery_grid.h"
#include "ui/unlock_screen.h"

#ifndef OSV_DEFAULT_FONT
#define OSV_DEFAULT_FONT "assets/fonts/NotoSans-Regular.ttf"
#endif

namespace app {

bool App::init()
{
    if (!window_.init()) {
        std::println(stderr, "[App] Window initialisation failed.");
        return false;
    }

    font_ready_ = font_.bake_from_file(OSV_DEFAULT_FONT, 28.0f);
    if (!font_ready_)
        std::println(stderr, "[App] Font atlas unavailable ('{}').", OSV_DEFAULT_FONT);

    cache_ = std::make_unique<gfx::TextureCache>(window_.sdl_renderer());
    to_unlock();

    std::println("[App] Initialised (Phase 5 — UI layer).");
    return true;
}

void App::to_unlock()
{
    state_  = State::Locked;
    screen_ = std::make_unique<ui::UnlockScreen>(window_, font_, vault_, dialog_,
                                                 platform::default_vault_path());
    screen_->on_enter();
}

void App::to_gallery()
{
    state_  = State::Browsing;
    screen_ = std::make_unique<ui::GalleryGrid>(window_, font_, vault_, *cache_, dialog_);
    screen_->on_enter();
}

void App::run()
{
    bool     running = true;
    uint64_t prev    = SDL_GetTicks();

    while (running) {
        SDL_Event e;
        while (window_.poll_event(e)) {
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                running = false;
            else if (screen_)
                screen_->handle_event(e);
        }

        const uint64_t now = SDL_GetTicks();
        const double   dt  = static_cast<double>(now - prev) / 1000.0;
        prev = now;

        if (screen_) screen_->update(dt);

        window_.begin_frame(18, 18, 24);
        if (screen_) {
            gfx::Renderer r(window_.sdl_renderer());
            screen_->render(r);
        }
        window_.end_frame();

        if (screen_) {
            switch (screen_->take_nav().kind) {
                case ui::NavKind::ToGallery: screen_->on_exit(); to_gallery(); break;
                case ui::NavKind::ToUnlock:  screen_->on_exit(); to_unlock();  break;
                case ui::NavKind::Quit:      running = false; break;
                case ui::NavKind::None:      break;
            }
        }
    }
}

void App::shutdown()
{
    if (screen_) { screen_->on_exit(); screen_.reset(); }
    vault_.lock();                 // wipe master key
    if (cache_) cache_->clear();   // destroy thumbnail textures before the renderer
    font_.release_texture();
    window_.shutdown();
    std::println("[App] Clean shutdown.");
}

} // namespace app
