#include "app.h"

#include <SDL3/SDL.h>

#include <print>
#include <string>

#include "app/auto_lock.h"
#include "app/keep_unlocked_badge.h"
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
#include "ui/import_model.h"
#include "ui/import_status_screen.h"
#include "ui/settings_overlay.h"
#include "ui/tag_galleries.h"
#include "ui/tag_images.h"
#include "ui/tag_overview.h"
#include "ui/unlock_screen.h"
#include "ui/vault_manager.h"
#include "ui/widgets.h"
#include "vault/vault_search.h"

#ifndef OSV_DEFAULT_FONT
#define OSV_DEFAULT_FONT "assets/fonts/NotoSans-Regular.ttf"
#endif

namespace app {

bool App::init()
{
    // A Windows Release build runs as a windowless (WindowedApp) subsystem
    // process, so stdout/stderr start with no valid OS handle at all — every
    // write to them fails, and C++23's std::print/std::println throw
    // std::system_error on such a failure (unlike old fprintf), which would
    // crash the whole process the first time any of the many existing
    // std::println(stderr, "[Module] ...") diagnostics ran. Redirect both to
    // a log file before anything else can print, so every diagnostic in the
    // app — previously invisible in a windowless build — becomes visible
    // instead of throwing (no-op on Linux/Debug, which keep a console).
#ifdef NDEBUG
    platform::redirect_diagnostics_to_log_file();
#endif

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
        window_, font_, registry_, dialog_, vault_state_.active ? vault_state_.active_path : std::string{});
    screen_->on_enter();
}

void App::to_unlock(const std::string& path)
{
    // App owns the vault the unlock screen operates on; on success it is promoted
    // to vault_state_.active. Create-vs-open is auto-selected by the screen from file existence.
    state_        = State::Locked;
    vault_state_.pending      = std::make_unique<vault::Vault>();
    vault_state_.pending_path = path;
    screen_ = std::make_unique<ui::UnlockScreen>(window_, font_, *vault_state_.pending, dialog_, path);
    screen_->on_enter();
}

void App::promote_pending()
{
    if (!vault_state_.pending) return;
    if (vault_state_.active) vault_state_.active->lock();                 // lock-on-switch: wipe the old key
    adv_session_   = {};                          // new vault session -> fresh advanced search
    session_.reset();                             // new vault session -> fresh gallery/viewer memory
    keep_unlocked_ = false;                       // new session always starts with auto-lock on
    vault_state_.active        = std::move(vault_state_.pending);
    vault_state_.active_path   = std::move(vault_state_.pending_path);
    vault_state_.pending_path.clear();
    registry_.add(vault_state_.active_path);                  // move-to-front in the recent list
    import_ui_.queue.begin_session(*vault_state_.active);        // Phase 50: start import queue for new vault
}

void App::to_gallery(const std::string& path, int selected, bool explicit_index)
{
    state_  = State::Browsing;
    const int seed = explicit_index ? selected : session_.recall(path);
    screen_ = std::make_unique<ui::GalleryGrid>(
        window_, font_, *vault_state_.active, *cache_,
        ui::GalleryGrid::GridDialogs{dialog_, folder_dialog_},
        ui::GalleryGrid::GridVaultCtx{registry_, vault_state_.active_path},
        session_, import_ui_.queue,
        ui::GridLocation{path, seed, session_.view});
    screen_->on_enter();
}

void App::enter_viewer(std::unique_ptr<ui::ImageViewer> viewer)
{
    viewer->on_enter();
    ui::apply_video_resume(*viewer, session_);   // resume a matching video, paused
    state_  = State::Viewing;
    screen_ = std::move(viewer);
}

void App::to_viewer(const std::string& gallery_path, int index)
{
    enter_viewer(std::make_unique<ui::ImageViewer>(
        window_, font_, *vault_state_.active, *cache_,
        ui::ImageViewer::Context{folder_dialog_, registry_, import_ui_.queue, vault_state_.active_path, session_.strip_side},
        ui::ImageViewer::Album::gallery(gallery_path), index));
}

void App::to_favorite_images()
{
    state_  = State::Browsing;
    auto screen = std::make_unique<ui::FavoritesImages>(
        window_, font_, *vault_state_.active, *cache_, registry_, vault_state_.active_path);
    screen->set_detail_open(session_.detail_open);
    screen_ = std::move(screen);
    screen_->on_enter();
}

void App::to_favorite_galleries()
{
    state_  = State::Browsing;
    auto screen = std::make_unique<ui::FavoritesGalleries>(
        window_, font_, *vault_state_.active, registry_, vault_state_.active_path);
    screen->set_detail_open(session_.detail_open);
    screen_ = std::move(screen);
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
    auto favs = vault_state_.active->list_favorite_images();
    album.images.reserve(favs.size());
    album.paths.reserve(favs.size());
    for (auto& h : favs) {
        album.images.push_back(h.node);
        album.paths.push_back(std::move(h.path));
    }

    enter_viewer(std::make_unique<ui::ImageViewer>(
        window_, font_, *vault_state_.active, *cache_,
        ui::ImageViewer::Context{folder_dialog_, registry_, import_ui_.queue, vault_state_.active_path, session_.strip_side},
        std::move(album), index));
}

void App::to_advanced_search()
{
    state_  = State::Browsing;
    screen_ = std::make_unique<ui::AdvancedSearchScreen>(window_, font_, *vault_state_.active, *cache_,
                                                         adv_session_, adv_session_.detail_open);
    screen_->on_enter();
}

void App::to_tag_overview()
{
    state_  = State::Browsing;
    screen_ = std::make_unique<ui::TagOverviewScreen>(
        window_, font_, *vault_state_.active, registry_, vault_state_.active_path);
    screen_->on_enter();
}

void App::to_tag_galleries(const std::string& tag)
{
    state_  = State::Browsing;
    auto screen = std::make_unique<ui::TagGalleries>(
        window_, font_, *vault_state_.active, registry_, vault_state_.active_path, tag);
    screen->set_detail_open(session_.detail_open);
    screen_ = std::move(screen);
    screen_->on_enter();
}

void App::to_tag_images(const std::string& tag)
{
    state_  = State::Browsing;
    auto screen = std::make_unique<ui::TagImages>(
        window_, font_, *vault_state_.active, *cache_, registry_, vault_state_.active_path, tag);
    screen->set_detail_open(session_.detail_open);
    screen_ = std::move(screen);
    screen_->on_enter();
}

void App::to_tag_viewer(const std::string& tag, int index)
{
    // Build a viewer collection from the tag's media set (same ordering the grid
    // used), so prev/next iterate the set and Esc returns to the tag-images grid.
    ui::ImageViewer::Album album;
    album.from_collection = true;
    album.back            = ui::Nav{ui::NavKind::ToTagImages, tag, 0};
    auto hits = vault::VaultSearch(*vault_state_.active).images_with_tag(tag);
    album.images.reserve(hits.size());
    album.paths.reserve(hits.size());
    for (auto& h : hits) {
        album.images.push_back(h.node);
        album.paths.push_back(std::move(h.path));
    }

    enter_viewer(std::make_unique<ui::ImageViewer>(
        window_, font_, *vault_state_.active, *cache_,
        ui::ImageViewer::Context{folder_dialog_, registry_, import_ui_.queue, vault_state_.active_path, session_.strip_side},
        std::move(album), index));
}

void App::to_import_status()
{
    using enum ui::NavKind;
    // Phase 50: derive the back nav from the outgoing screen before teardown
    if (!vault_state_.active) return;  // guard: only if a vault is unlocked

    ui::Nav back;

    if (const auto* grid = dynamic_cast<const ui::GalleryGrid*>(screen_.get())) {
        // GalleryGrid: return to the same path at index 0
        back = ui::Nav{ToGallery, ui::current_gallery_path(*grid), 0};
    } else if (dynamic_cast<const ui::ImageViewer*>(screen_.get())) {
        // ImageViewer: return to the gallery root
        back = ui::Nav{ToGallery, {}, 0};
    } else if (dynamic_cast<const ui::VaultManager*>(screen_.get())) {
        // VaultManager: return to the vault manager
        back = ui::Nav{ToVaultManager, {}, 0};
    } else {
        // All other screens: return to gallery root
        back = ui::Nav{ToGallery, {}, 0};
    }

    state_  = State::Browsing;
    screen_ = std::make_unique<ui::ImportStatusScreen>(window_, font_, import_ui_.queue, back);
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

// A small, unmissable corner badge shown on every screen while the Phase 33
// "keep unlocked" toggle suppresses the idle auto-lock — drawn by App (not a
// Screen) so it stays visible across navigation without threading session
// state through every screen's constructor.
void draw_keep_unlocked_badge(gfx::Renderer& r, gfx::FontAtlas& font, int win_w, int win_h)
{
    using namespace gfx::theme;
    static constexpr const char* LABEL  = "Auto-lock off [U]";
    static constexpr float       PAD    = 10.0f;
    static constexpr float       MARGIN = 16.0f;

    const auto  tw = static_cast<float>(font.measure(LABEL));
    const float th = font.pixel_height();
    const float bw = tw + (PAD * 2);
    const float bh = th + (PAD * 2);
    const SDL_FRect box{static_cast<float>(win_w) - bw - MARGIN,
                        static_cast<float>(win_h) - bh - MARGIN, bw, bh};
    r.draw_round_rect(box, RADIUS_SMALL, SURFACE);
    r.draw_round_rect(box, RADIUS_SMALL, WARN, /*filled*/ false);
    r.draw_text(font, box.x + PAD, box.y + PAD, LABEL, WARN);
}
} // namespace

bool App::dispatch_overlay_event(App& app, const SDL_Event& e)
{
    // F1 toggles help (checked before help.open guard so it opens/closes over settings)
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F1) {
        ui::toggle_help(app.overlays_.help);
        return true;
    }
    // Help popup (highest priority: swallows arrow/wheel; over settings)
    if (app.overlays_.help.open) {
        if (e.type == SDL_EVENT_KEY_DOWN) {
            ui::handle_help_key(app.overlays_.help, e.key.key);
        } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            ui::handle_help_wheel(app.overlays_.help, e.wheel.y);
        }
        return true;
    }
    // F2 toggles settings
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F2) {
        if (app.overlays_.settings.open) {
            ui::close_settings(app.overlays_.settings, app.window_);
        } else {
            app.open_settings_overlay();
        }
        return true;
    }
    // Settings panel (second priority: swallows all events)
    if (app.overlays_.settings.open) {
        if (bool commit = false;
            ui::handle_settings_event(app.overlays_.settings, app.window_, e, commit) && commit &&
            app.overlays_.settings.vault_unlocked && app.vault_state_.active &&
            vault::set_vault_settings(*app.vault_state_.active, app.overlays_.settings.draft) != vault::VaultResult::Ok) {
            app.overlays_.settings.error = "Could not save settings";
        }
        return true;
    }
    // Lock-confirm modal (lowest priority: key events only; Phase 50)
    if (app.import_ui_.lock_confirm.open) {
        if (e.type == SDL_EVENT_KEY_DOWN) {
            using enum ui::LockConfirmKey;
            const auto key = ui::classify_lock_confirm_key(e.key.key);
            if (key == Confirm) {
                app.import_ui_.queue.abort_and_flush();
                app.import_ui_.replay_nav = app.import_ui_.lock_confirm.action;
                app.import_ui_.lock_confirm = {};
            } else if (key == Cancel) {
                app.import_ui_.lock_confirm = {};
            }
        }
        return true;
    }
    return false;
}

void App::dispatch_event(const SDL_Event& e)
{
    if (is_user_input(e)) idle_.reset();
    // Phase 50: park SDL_EVENT_QUIT if imports are pending; replayed after confirm
    if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        if (import_ui_.queue.busy() && !import_ui_.lock_confirm.open) {
            import_ui_.lock_confirm = {true, ui::Nav{ui::NavKind::Quit, {}, 0}};
            return;
        }
        running_ = false;
        return;
    }
    // Dispatch to overlays (guards QUIT, modals, etc.); returns true if handled
    if (dispatch_overlay_event(*this, e))
        return;
    if (screen_) screen_->handle_event(e);
}

bool App::pump_events(bool animating)
{
    SDL_Event e;
    bool      should_redraw = false;
    if (animating) {
        // Keep ticking the animation: never block, just drain the queue.
        while (window_.poll_event(e)) { dispatch_event(e); should_redraw = true; }
    } else if (SDL_WaitEventTimeout(&e, IDLE_HEARTBEAT_MS)) {
        // Idle: block until an event (or the heartbeat) rather than spinning.
        dispatch_event(e);
        should_redraw = true;
        while (window_.poll_event(e)) dispatch_event(e);
    } else if (window_.is_visible()) {
        // Heartbeat woke with no event: redraw anyway (a static frame; nothing
        // changed). On Windows with G-SYNC + Auto HDR, letting the swapchain go
        // fully idle (zero presents) lets the display renegotiate refresh rate /
        // tone-mapping for a static scene; the burst of frames the next
        // mouse-move triggers then reads as a visible brightness pulse when that
        // negotiation snaps back. Presenting on every ~250ms heartbeat keeps
        // cadence bounded instead of falling to zero. Skipped when the window
        // isn't actually on screen (minimized/hidden/occluded) — nothing to fix
        // there, so don't burn GPU on it.
        should_redraw = true;
    }
    return should_redraw;
}

void App::capture_session_state()
{
    // Snapshot the outgoing screen's view/strip-side/video-position into
    // session_ before it is destroyed (Phase 39 Part 2) — must run before
    // on_exit(), which tears down ImageViewer's live video_.
    if (const auto* grid = dynamic_cast<const ui::GalleryGrid*>(screen_.get())) {
        session_.view = ui::current_gallery_view(*grid);
    } else if (const auto* viewer = dynamic_cast<const ui::ImageViewer*>(screen_.get())) {
        session_.strip_side = ui::current_strip_side(*viewer);
        ui::capture_video_resume(*viewer, session_);
    } else if (const auto* fav = dynamic_cast<const ui::FavoritesScreen*>(screen_.get())) {
        session_.detail_open = ui::current_detail_open(*fav);
    } else if (const auto* adv = dynamic_cast<const ui::AdvancedSearchScreen*>(screen_.get())) {
        adv_session_.detail_open = ui::current_detail_open(*adv);
    }
}

void App::open_settings_overlay()
{
    overlays_.settings.vault_unlocked = vault_state_.active && vault_state_.active->is_unlocked();
    overlays_.settings.draft = overlays_.settings.vault_unlocked ? vault::vault_settings(*vault_state_.active)
                                                                  : vault::VaultSettings{};
    overlays_.settings.theme = gfx::active_theme_id();
    ui::open_settings(overlays_.settings, ui::SettingsSection::Appearance);
}

bool App::apply_nav()
{
    if (!screen_) return false;
    using enum ui::NavKind;
    // Phase 50: check for a replayed nav (from lock_confirm confirm) first
    ui::Nav nav = import_ui_.replay_nav.kind != ui::NavKind::None ? import_ui_.replay_nav : screen_->take_nav();
    if (nav.kind != ui::NavKind::None) import_ui_.replay_nav = {};   // consume import_ui_.replay_nav after taking it
    // A ToGallery nav.index is only a real, freshly-known position when it
    // comes from the viewer returning to its exact launch position (Phase 40
    // Part 2) — every other ToGallery source passes 0 as "no opinion" and
    // to_gallery() falls back to the remembered position for that path.
    const bool from_viewer = dynamic_cast<const ui::ImageViewer*>(screen_.get()) != nullptr;

    // Phase 50: park lock-ish actions that occur while imports are pending.
    // These actions will be replayed after the user confirms the import abort.
    if ((nav.kind == LockActive || nav.kind == ToUnlock || nav.kind == Quit) &&
        import_ui_.queue.busy() && !import_ui_.lock_confirm.open) {
        import_ui_.lock_confirm = {true, nav};
        return false;   // screen stays; event will be re-queued by dispatch_event
    }

    // Every transition below except ToggleKeepUnlocked/ToSettings/Quit/None destroys the
    // current screen.
    if (nav.kind != None && nav.kind != ToggleKeepUnlocked && nav.kind != ToSettings &&
        nav.kind != Quit) {
        capture_session_state();
        screen_->on_exit();
    }
    switch (nav.kind) {
        case ToGallery:
            if (state_ == State::Locked) promote_pending();   // unlock-screen success
            if (vault_state_.active) to_gallery(nav.path, nav.index, from_viewer);
            else         to_manager();                        // defensive: nothing unlocked
            return true;
        case ToViewer:            to_viewer(nav.path, nav.index);      return true;
        case ToFavoriteImages:    to_favorite_images();                return true;
        case ToFavoriteGalleries: to_favorite_galleries();             return true;
        case ToFavoriteViewer:    to_favorite_viewer(nav.index);       return true;
        case ToAdvancedSearch:    to_advanced_search();                return true;
        case ToTagOverview:       to_tag_overview();                   return true;
        case ToTagGalleries:      to_tag_galleries(nav.path);          return true;
        case ToTagImages:         to_tag_images(nav.path);             return true;
        case ToTagViewer:         to_tag_viewer(nav.path, nav.index);  return true;
        case ToImportStatus:      to_import_status(); return true;  // Phase 50
        case ToUnlock:
            import_ui_.queue.end_session();          // Phase 50: flush before switch
            to_unlock(nav.path);
            return true;
        case ToVaultManager:      vault_state_.pending.reset(); to_manager();      return true;
        case LockActive:
            keep_unlocked_ = false;
            session_.reset();                     // Phase 39 Part 2: fresh session on lock
            import_ui_.queue.end_session();          // Phase 50: flush before lock
            if (vault_state_.active) { vault_state_.active->lock(); vault_state_.active.reset(); vault_state_.active_path.clear(); }
            to_manager();
            return true;
        case ToSettings:
            // Stays on the current screen: the overlay draws over it, so no
            // on_exit()/screen swap — same shape as ToggleKeepUnlocked.
            open_settings_overlay();
            return true;
        case ToggleKeepUnlocked:
            // Stays on the current screen: no on_exit()/screen swap, just flip the
            // session flag and reset the idle timer (see should_auto_lock) so
            // re-disabling doesn't inherit a stale elapsed value.
            keep_unlocked_ = !keep_unlocked_;
            idle_.reset();
            if (keep_unlocked_) badge_elapsed_ = 0.0;   // Phase 45 Part 6: fresh 10s window
            return true;
        case Quit:                running_ = false;                                    return false;
        case None:                return false;
    }
    return false;
}

bool App::maybe_auto_lock(double dt)
{
    // should_auto_lock also covers "a screen with a background import owns the
    // vault's file handle on a worker thread" (blocks_idle_lock), the session's
    // "keep unlocked" toggle (Phase 33), and a busy import queue (Phase 50) —
    // see app/auto_lock.h.
    if (const bool blocks = screen_ && screen_->blocks_idle_lock();
        !should_auto_lock(vault_state_.active != nullptr, blocks, keep_unlocked_, import_ui_.queue.busy(),
                          idle_, dt))
        return false;
    if (screen_) screen_->on_exit();
    session_.reset();                                  // Phase 39 Part 2: fresh session on idle lock
    import_ui_.queue.end_session();                       // Phase 50: flush before lock
    vault_state_.active->lock();                                  // wipe the master key
    vault_state_.active.reset();
    vault_state_.active_path.clear();
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
        if (vault_state_.active && should_show_badge(keep_unlocked_, badge_elapsed_, BADGE_WINDOW_SECS))
            draw_keep_unlocked_badge(r, font_, window_.width(), window_.height());
        if (overlays_.settings.open) {
            ui::draw_settings_overlay(r, font_, static_cast<float>(window_.width()),
                                      static_cast<float>(window_.height()), overlays_.settings);
        }
        // Phase 50: render lock_confirm modal after settings overlay so it stays on top
        if (import_ui_.lock_confirm.open) {
            const auto w = static_cast<float>(window_.width());
            const auto h = static_cast<float>(window_.height());
            using namespace gfx::theme;

            // Veil the whole window so the modal clearly owns input focus
            r.draw_rect({0, 0, w, h}, gfx::Color{8, 9, 12, 255});

            const float pw = 560;
            const float ph = 230;
            const float px = (w - pw) / 2;
            const float py = (h - ph) / 2;
            r.draw_round_rect({px, py, pw, ph}, RADIUS, SURFACE);
            r.draw_round_rect({px, py, pw, ph}, RADIUS, DANGER, /*filled*/ false);

            auto centered = [&](const std::string& s, float y, gfx::Color c) {
                const auto tw = static_cast<float>(font_.measure(s));
                r.draw_text(font_, px + (pw - tw) / 2, y, s, c);
            };

            const std::string confirm_text = ui::import_lock_confirm_text(
                static_cast<int>(import_ui_.queue.snapshot().size()));
            centered(ui::fit_text(font_, confirm_text, pw - 32), py + 28, TEXT);
            centered("[Y] Discard & lock        [N] Keep importing", py + ph - 50, TEXT_DIM);
        }
        ui::draw_help_popup(r, font_, static_cast<float>(window_.width()),
                            static_cast<float>(window_.height()),
                            screen_->help_groups(), overlays_.help);
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
        const bool animating     = screen_ && screen_->animating();
        const bool should_redraw = pump_events(animating);

        const uint64_t now = SDL_GetTicksNS();
        const double   dt  = static_cast<double>(now - prev) / 1'000'000'000.0;
        prev = now;

        if (screen_) screen_->update(dt);
        badge_elapsed_ += dt;   // Phase 45 Part 6

        // Phase 50: drain the import queue and refresh screens when records are applied
        if (vault_state_.active && import_ui_.queue.drain(dt) > 0 && screen_) {
            screen_->on_vault_changed();
            screen_->mark_dirty();
        }

        // Idle auto-lock runs before nav resolution so the manager paints this frame.
        const bool auto_locked = maybe_auto_lock(dt);

        // Resolve a transition before rendering so the destination screen paints
        // this frame instead of after another idle heartbeat.
        const bool transitioned = apply_nav();

        bool redraw = animating || should_redraw || transitioned || auto_locked;
        if (screen_ && screen_->consume_dirty()) redraw = true;
        if (running_ && redraw) render_frame();
    }

    // Persist the remembered playback volume on a clean exit (Phase 25 follow-up).
    platform::VolumePref::default_location().save(media::saved_volume());
}

void App::shutdown()
{
    if (screen_) { screen_->on_exit(); screen_.reset(); }
    import_ui_.queue.end_session();        // Phase 50: flush before lock (blocking, acceptable at shutdown)
    if (vault_state_.active)  vault_state_.active->lock();      // wipe master key
    if (vault_state_.pending) vault_state_.pending->lock();
    vault_state_.active.reset();
    vault_state_.pending.reset();
    if (cache_) cache_->clear();        // destroy thumbnail textures before the renderer
    font_.release_texture();
    window_.shutdown();
    std::println("[App] Clean shutdown.");
}

} // namespace app
