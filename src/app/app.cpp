#include "app.h"

#include <SDL3/SDL.h>

#include <print>
#include <string>

#include "gfx/renderer.h"
#include "gfx/theme.h"
#include "platform/error_log.h"
#include "platform/harden.h"
#include "platform/paths.h"
#include "media/volume_setting.h"
#include "platform/theme_pref.h"
#include "platform/volume_pref.h"
#include "ui/advanced_search_screen.h"
#include "ui/favorites_galleries.h"
#include "ui/favorites_images.h"
#include "ui/gallery_grid.h"
#include "ui/image_viewer.h"
#include "ui/tag_galleries.h"
#include "ui/tag_images.h"
#include "ui/tag_overview.h"
#include "ui/unlock_screen.h"
#include "ui/vault_manager.h"
#include "vault/vault_search.h"

#ifndef OSV_DEFAULT_FONT
#define OSV_DEFAULT_FONT "assets/fonts/NotoSans-Regular.ttf"
#endif

namespace app {

bool App::init()
{
    // Log-before-die: an uncaught exception anywhere (e.g. std::bad_alloc from
    // an STL container) would otherwise call std::terminate() and vanish with
    // zero trace, since Release is a windowless app with no console. Install
    // this first, before anything else can throw.
    platform::install_terminate_logger();

    // Disable core dumps in Release builds to prevent decrypted data / key material
    // from being dumped to disk. In Debug, core dumps and ptrace attach are kept
    // enabled for developers to use debuggers and analyze crashes.
#ifdef NDEBUG
    platform::disable_core_dumps();
#endif

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

    // Apply the saved UI theme before drawing the first frame (Phase 23).
    gfx::set_theme(platform::ThemePref::default_location().load());

    // Restore the remembered media playback volume (Phase 25 follow-up); persisted
    // again on exit at the end of run().
    media::set_saved_volume(platform::VolumePref::default_location().load());

    registry_ = platform::VaultRegistry::default_location();
    registry_.seed_if_empty(platform::default_vault_path());
    to_manager();

    std::println("[App] Initialised (Phase 14 — multiple vaults).");
    return true;
}

void App::to_manager()
{
    state_  = State::Managing;
    screen_ = std::make_unique<ui::VaultManager>(
        window_, font_, registry_, dialog_, active_ ? active_path_ : std::string{});
    screen_->on_enter();
}

void App::to_unlock(const std::string& path)
{
    // App owns the vault the unlock screen operates on; on success it is promoted
    // to active_. Create-vs-open is auto-selected by the screen from file existence.
    state_        = State::Locked;
    pending_      = std::make_unique<vault::Vault>();
    pending_path_ = path;
    screen_ = std::make_unique<ui::UnlockScreen>(window_, font_, *pending_, dialog_, path);
    screen_->on_enter();
}

void App::promote_pending()
{
    if (!pending_) return;
    if (active_) active_->lock();                 // lock-on-switch: wipe the old key
    adv_session_ = {};                            // new vault session -> fresh advanced search
    active_      = std::move(pending_);
    active_path_ = std::move(pending_path_);
    pending_path_.clear();
    registry_.add(active_path_);                  // move-to-front in the recent list
}

void App::to_gallery(const std::string& path, int selected)
{
    state_  = State::Browsing;
    screen_ = std::make_unique<ui::GalleryGrid>(
        window_, font_, *active_, *cache_,
        ui::GalleryGrid::GridDialogs{dialog_, folder_dialog_},
        ui::GalleryGrid::GridVaultCtx{registry_, active_path_},
        ui::GridLocation{path, selected});
    screen_->on_enter();
}

void App::to_viewer(const std::string& gallery_path, int index)
{
    state_  = State::Viewing;
    screen_ = std::make_unique<ui::ImageViewer>(
        window_, font_, *active_, *cache_,
        ui::ImageViewer::Context{folder_dialog_, registry_, active_path_},
        ui::ImageViewer::Album::gallery(gallery_path), index);
    screen_->on_enter();
}

void App::to_favorite_images()
{
    state_  = State::Browsing;
    screen_ = std::make_unique<ui::FavoritesImages>(
        window_, font_, *active_, *cache_, registry_, active_path_);
    screen_->on_enter();
}

void App::to_favorite_galleries()
{
    state_  = State::Browsing;
    screen_ = std::make_unique<ui::FavoritesGalleries>(
        window_, font_, *active_, registry_, active_path_);
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
    auto favs = active_->list_favorite_images();
    album.images.reserve(favs.size());
    album.paths.reserve(favs.size());
    for (auto& h : favs) {
        album.images.push_back(h.node);
        album.paths.push_back(std::move(h.path));
    }

    state_  = State::Viewing;
    screen_ = std::make_unique<ui::ImageViewer>(
        window_, font_, *active_, *cache_,
        ui::ImageViewer::Context{folder_dialog_, registry_, active_path_},
        std::move(album), index);
    screen_->on_enter();
}

void App::to_advanced_search()
{
    state_  = State::Browsing;
    screen_ = std::make_unique<ui::AdvancedSearchScreen>(window_, font_, *active_, *cache_,
                                                         adv_session_);
    screen_->on_enter();
}

void App::to_tag_overview()
{
    state_  = State::Browsing;
    screen_ = std::make_unique<ui::TagOverviewScreen>(
        window_, font_, *active_, registry_, active_path_);
    screen_->on_enter();
}

void App::to_tag_galleries(const std::string& tag)
{
    state_  = State::Browsing;
    screen_ = std::make_unique<ui::TagGalleries>(
        window_, font_, *active_, registry_, active_path_, tag);
    screen_->on_enter();
}

void App::to_tag_images(const std::string& tag)
{
    state_  = State::Browsing;
    screen_ = std::make_unique<ui::TagImages>(
        window_, font_, *active_, *cache_, registry_, active_path_, tag);
    screen_->on_enter();
}

void App::to_tag_viewer(const std::string& tag, int index)
{
    // Build a viewer collection from the tag's media set (same ordering the grid
    // used), so prev/next iterate the set and Esc returns to the tag-images grid.
    ui::ImageViewer::Album album;
    album.from_collection = true;
    album.back            = ui::Nav{ui::NavKind::ToTagImages, tag, 0};
    auto hits = vault::VaultSearch(*active_).images_with_tag(tag);
    album.images.reserve(hits.size());
    album.paths.reserve(hits.size());
    for (auto& h : hits) {
        album.images.push_back(h.node);
        album.paths.push_back(std::move(h.path));
    }

    state_  = State::Viewing;
    screen_ = std::make_unique<ui::ImageViewer>(
        window_, font_, *active_, *cache_,
        ui::ImageViewer::Context{folder_dialog_, registry_, active_path_},
        std::move(album), index);
    screen_->on_enter();
}

namespace {
// Manual frame-rate floor, used only when the renderer can't VSync (software /
// headless backends); otherwise SDL_RenderPresent paces presentation.
constexpr uint64_t FRAME_CAP_NS = 1'000'000'000ULL / 60;
// Upper bound on how long the loop blocks for input while idle, so async results
// (file dialogs, the decode worker) surface promptly even without a wake event.
constexpr int32_t IDLE_HEARTBEAT_MS = 250;

// Whether an event is direct user input (resets the idle-lock timer). Window
// events, the decode-worker wake, and async dialog results deliberately don't.
bool is_user_input(const SDL_Event& e) noexcept
{
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
            return true;
        default:
            return false;
    }
}
} // namespace

void App::dispatch_event(const SDL_Event& e)
{
    if (is_user_input(e)) idle_.reset();   // any keypress/mouse activity stays the lock
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
        case ToGallery:
            screen_->on_exit();
            if (state_ == State::Locked) promote_pending();   // unlock-screen success
            if (active_) to_gallery(nav.path, nav.index);
            else         to_manager();                        // defensive: nothing unlocked
            return true;
        case ToViewer:            screen_->on_exit(); to_viewer(nav.path, nav.index);  return true;
        case ToFavoriteImages:    screen_->on_exit(); to_favorite_images();            return true;
        case ToFavoriteGalleries: screen_->on_exit(); to_favorite_galleries();         return true;
        case ToFavoriteViewer:    screen_->on_exit(); to_favorite_viewer(nav.index);   return true;
        case ToAdvancedSearch:    screen_->on_exit(); to_advanced_search();            return true;
        case ToTagOverview:       screen_->on_exit(); to_tag_overview();               return true;
        case ToTagGalleries:      screen_->on_exit(); to_tag_galleries(nav.path);      return true;
        case ToTagImages:         screen_->on_exit(); to_tag_images(nav.path);          return true;
        case ToTagViewer:         screen_->on_exit(); to_tag_viewer(nav.path, nav.index); return true;
        case ToUnlock:            screen_->on_exit(); to_unlock(nav.path);             return true;
        case ToVaultManager:      screen_->on_exit(); pending_.reset(); to_manager();  return true;
        case LockActive:
            screen_->on_exit();
            if (active_) { active_->lock(); active_.reset(); active_path_.clear(); }
            to_manager();
            return true;
        case Quit:                running_ = false;                                    return false;
        case None:                return false;
    }
    return false;
}

bool App::maybe_auto_lock(double dt)
{
    if (!active_) { idle_.reset(); return false; }   // nothing to lock
    // A screen with a background import owns the vault's file handle on a worker
    // thread; auto-locking now would wipe the master key mid-write. Stay awake.
    if (screen_ && screen_->blocks_idle_lock()) { idle_.reset(); return false; }
    if (!idle_.tick(dt)) return false;
    if (screen_) screen_->on_exit();
    active_->lock();                                  // wipe the master key
    active_.reset();
    active_path_.clear();
    to_manager();
    std::println("[App] Auto-locked after {} s idle.", static_cast<int>(IDLE_LOCK_SECS));
    return true;
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

        // Idle auto-lock runs before nav resolution so the manager paints this frame.
        const bool auto_locked = maybe_auto_lock(dt);

        // Resolve a transition before rendering so the destination screen paints
        // this frame instead of after another idle heartbeat.
        const bool transitioned = apply_nav();

        bool redraw = animating || had_event || transitioned || auto_locked;
        if (screen_ && screen_->consume_dirty()) redraw = true;
        if (running_ && redraw) render_frame();
    }

    // Persist the remembered playback volume on a clean exit (Phase 25 follow-up).
    platform::VolumePref::default_location().save(media::saved_volume());
}

void App::shutdown()
{
    if (screen_) { screen_->on_exit(); screen_.reset(); }
    if (active_)  active_->lock();      // wipe master key
    if (pending_) pending_->lock();
    active_.reset();
    pending_.reset();
    if (cache_) cache_->clear();        // destroy thumbnail textures before the renderer
    font_.release_texture();
    window_.shutdown();
    std::println("[App] Clean shutdown.");
}

} // namespace app
