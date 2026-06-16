#include "app.h"

#include <SDL3/SDL.h>

#include <print>
#include <string>

#include "gfx/renderer.h"
#include "gfx/theme.h"
#include "platform/paths.h"
#include "ui/gallery_grid.h"
#include "ui/image_viewer.h"
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

    // Dev runs launch from the repo root (cwd-relative); packaged apps resolve
    // assets next to the executable (= Contents/Resources inside a mac bundle).
    font_ready_ = font_.bake_from_file(OSV_DEFAULT_FONT, 28.0f);
    if (!font_ready_) {
        if (const char* base = SDL_GetBasePath(); base) {
            const std::string fallback = std::string{base} + OSV_DEFAULT_FONT;
            font_ready_ = font_.bake_from_file(fallback.c_str(), 28.0f);
        }
    }
    if (!font_ready_)
        std::println(stderr, "[App] Font atlas unavailable ('{}').", OSV_DEFAULT_FONT);

    cache_ = std::make_unique<gfx::TextureCache>(window_.sdl_renderer());

    // One SDL event type for decode-worker wake-ups (shared by all screens'
    // workers). SDL_RegisterEvents returns (Uint32)-1 on failure; treat that as
    // "no wake" and lean on the loop's idle heartbeat instead.
    decode_wake_ = SDL_RegisterEvents(1);
    if (decode_wake_ == static_cast<uint32_t>(-1)) decode_wake_ = 0;

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

void App::to_gallery(const std::string& path, int selected)
{
    state_  = State::Browsing;
    screen_ = std::make_unique<ui::GalleryGrid>(window_, font_, vault_, *cache_, dialog_,
                                                folder_dialog_, decode_wake_,
                                                ui::GridLocation{path, selected});
    screen_->on_enter();
}

void App::to_viewer(const std::string& gallery_path, int index)
{
    state_  = State::Viewing;
    screen_ = std::make_unique<ui::ImageViewer>(window_, font_, vault_, *cache_,
                                                folder_dialog_, decode_wake_,
                                                gallery_path, index);
    screen_->on_enter();
}

void App::run()
{
    bool running = true;

    // Manual frame-rate floor, used only when the renderer can't VSync (software
    // / headless backends); otherwise SDL_RenderPresent paces presentation.
    constexpr uint64_t FRAME_CAP_NS = 1'000'000'000ULL / 60;
    // Upper bound on how long we block waiting for input while idle, so async
    // results (file dialogs, the decode worker) surface promptly even without an
    // explicit wake-up event.
    constexpr int32_t IDLE_HEARTBEAT_MS = 250;

    uint64_t prev = SDL_GetTicksNS();

    // Handle quit/close here; forward every other event to the active screen.
    const auto dispatch = [&](const SDL_Event& e) {
        if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            running = false;
        else if (screen_)
            screen_->handle_event(e);
    };

    while (running) {
        const bool animating = screen_ && screen_->animating();

        SDL_Event e;
        bool      had_event = false;
        if (animating) {
            // Keep ticking the animation: never block, just drain the queue.
            while (window_.poll_event(e)) { dispatch(e); had_event = true; }
        } else {
            // Idle: block until an event (or the heartbeat) rather than spinning.
            if (SDL_WaitEventTimeout(&e, IDLE_HEARTBEAT_MS)) {
                dispatch(e);
                had_event = true;
                while (window_.poll_event(e)) dispatch(e);
            }
        }

        const uint64_t now = SDL_GetTicksNS();
        const double   dt  = static_cast<double>(now - prev) / 1'000'000'000.0;
        prev = now;

        if (screen_) screen_->update(dt);

        // Resolve a transition request before rendering so the destination
        // screen paints this frame instead of after another idle heartbeat.
        bool transitioned = false;
        if (screen_) {
            using enum ui::NavKind;
            const ui::Nav nav = screen_->take_nav();
            switch (nav.kind) {
                case ToGallery: screen_->on_exit(); to_gallery(nav.path, nav.index); transitioned = true; break;
                case ToViewer:  screen_->on_exit(); to_viewer(nav.path, nav.index);  transitioned = true; break;
                case ToUnlock:  screen_->on_exit(); to_unlock();  transitioned = true; break;
                case Quit:      running = false; break;
                case None:      break;
            }
        }

        bool redraw = animating || had_event || transitioned;
        if (screen_ && screen_->consume_dirty()) redraw = true;

        if (running && redraw) {
            const uint64_t render_start = SDL_GetTicksNS();
            window_.begin_frame(gfx::theme::BG.r, gfx::theme::BG.g, gfx::theme::BG.b);
            if (screen_) {
                gfx::Renderer r(window_.sdl_renderer());
                screen_->render(r);
            }
            window_.end_frame();

            if (!window_.vsync()) {
                const uint64_t spent = SDL_GetTicksNS() - render_start;
                if (spent < FRAME_CAP_NS) SDL_DelayNS(FRAME_CAP_NS - spent);
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
