#include "app.h"

#include <SDL3/SDL.h>

#include <print>
#include <string>

#include "gfx/renderer.h"
#include "gfx/theme.h"
#include "platform/paths.h"
#include "ui/favorites_galleries.h"
#include "ui/favorites_images.h"
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
                                                folder_dialog_,
                                                ui::GridLocation{path, selected});
    screen_->on_enter();
}

void App::to_viewer(const std::string& gallery_path, int index)
{
    state_  = State::Viewing;
    screen_ = std::make_unique<ui::ImageViewer>(
        window_, font_, vault_, *cache_, folder_dialog_,
        ui::ImageViewer::Album::gallery(gallery_path), index);
    screen_->on_enter();
}

void App::to_favorite_images()
{
    state_  = State::Browsing;
    screen_ = std::make_unique<ui::FavoritesImages>(window_, font_, vault_, *cache_);
    screen_->on_enter();
}

void App::to_favorite_galleries()
{
    state_  = State::Browsing;
    screen_ = std::make_unique<ui::FavoritesGalleries>(window_, font_, vault_);
    screen_->on_enter();
}

void App::to_favorite_viewer(int index)
{
    // Build a viewer collection from the favorites set (same ordering the
    // favorites grid used), so prev/next iterate the favorites. Exiting returns
    // to the favorites-images grid.
    ui::ImageViewer::Album album;
    album.from_collection = true;
    album.back            = ui::Nav{ui::NavKind::ToFavoriteImages, {}, 0};
    auto favs = vault_.list_favorite_images();
    album.images.reserve(favs.size());
    album.paths.reserve(favs.size());
    for (auto& h : favs) {
        album.images.push_back(h.node);
        album.paths.push_back(std::move(h.path));
    }

    state_  = State::Viewing;
    screen_ = std::make_unique<ui::ImageViewer>(
        window_, font_, vault_, *cache_, folder_dialog_, std::move(album), index);
    screen_->on_enter();
}

namespace {
// Manual frame-rate floor, used only when the renderer can't VSync (software /
// headless backends); otherwise SDL_RenderPresent paces presentation.
constexpr uint64_t FRAME_CAP_NS = 1'000'000'000ULL / 60;
// Upper bound on how long the loop blocks for input while idle, so async results
// (file dialogs, the decode worker) surface promptly even without a wake event.
constexpr int32_t IDLE_HEARTBEAT_MS = 250;
} // namespace

void App::dispatch_event(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        running_ = false;
    else if (screen_)
        screen_->handle_event(e);
}

bool App::pump_events(bool animating)
{
    SDL_Event e;
    bool      had_event = false;
    if (animating) {
        // Keep ticking the animation: never block, just drain the queue.
        while (window_.poll_event(e)) { dispatch_event(e); had_event = true; }
    } else if (SDL_WaitEventTimeout(&e, IDLE_HEARTBEAT_MS)) {
        // Idle: block until an event (or the heartbeat) rather than spinning.
        dispatch_event(e);
        had_event = true;
        while (window_.poll_event(e)) dispatch_event(e);
    }
    return had_event;
}

bool App::apply_nav()
{
    if (!screen_) return false;
    using enum ui::NavKind;
    switch (const ui::Nav nav = screen_->take_nav(); nav.kind) {
        case ToGallery: screen_->on_exit(); to_gallery(nav.path, nav.index); return true;
        case ToViewer:  screen_->on_exit(); to_viewer(nav.path, nav.index);  return true;
        case ToFavoriteImages:    screen_->on_exit(); to_favorite_images();    return true;
        case ToFavoriteGalleries: screen_->on_exit(); to_favorite_galleries(); return true;
        case ToFavoriteViewer:    screen_->on_exit(); to_favorite_viewer(nav.index); return true;
        case ToUnlock:  screen_->on_exit(); to_unlock();                     return true;
        case Quit:      running_ = false;                                    return false;
        case None:      return false;
    }
    return false;
}

void App::render_frame()
{
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

void App::run()
{
    running_       = true;
    uint64_t prev  = SDL_GetTicksNS();

    while (running_) {
        const bool animating = screen_ && screen_->animating();
        const bool had_event = pump_events(animating);

        const uint64_t now = SDL_GetTicksNS();
        const double   dt  = static_cast<double>(now - prev) / 1'000'000'000.0;
        prev = now;

        if (screen_) screen_->update(dt);

        // Resolve a transition before rendering so the destination screen paints
        // this frame instead of after another idle heartbeat.
        const bool transitioned = apply_nav();

        bool redraw = animating || had_event || transitioned;
        if (screen_ && screen_->consume_dirty()) redraw = true;
        if (running_ && redraw) render_frame();
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
