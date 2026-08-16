#include "app.h"

#include <SDL3/SDL.h>

#include <print>
#include <string>

#include "app/auto_lock.h"
#include "app/back_click.h"
#include "app/keep_unlocked_badge.h"
#include "gfx/renderer.h"
#include "gfx/theme.h"
#include "platform/error_log.h"
#include "platform/gallery_view_pref.h"
#include "platform/harden.h"
#include "platform/paths.h"
#include "platform/path_utf8.h"
#include "platform/second_vault_pref.h"
#include "media/volume_setting.h"
#include "media/autoplay_setting.h"
#include "platform/theme_pref.h"
#include "platform/volume_pref.h"
#include "platform/autoplay_pref.h"
#include "platform/perf.h"
#include "ui/advanced_search_screen.h"
#include "ui/dual_gallery.h"
#include "ui/duplicates_screen.h"
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
#include "ui/progress_modal.h"
#include "ui/delete_summary.h"
#include "ui/meta_format.h"
#include "vault/vault_search.h"
#include "media/video_probe.h"
#include "image/thumbnail.h"

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

    // Grow the page-lockable budget BEFORE anything allocates a SecureBuffer/
    // SecureBytes: on Windows VirtualLock is capped by the minimum working-set
    // size (~200 KB by default — below a single decoded image, so every pixel
    // buffer silently degraded to swappable memory); on Linux this raises the
    // soft RLIMIT_MEMLOCK to the hard limit. 256 MiB covers the viewer's
    // decoded image + the thumbnail strip; larger buffers keep the documented
    // warn-once best-effort behaviour. All build configs: this is not a
    // debugging tradeoff like the core-dump gate above.
    if (constexpr size_t SECURE_MEM_BUDGET = size_t{256} << 20;
        !platform::grow_secure_mem_budget(SECURE_MEM_BUDGET)) {
        std::println(stderr, "[App] secure-memory budget below {} MiB — "
                     "large decoded images may not be page-locked.",
                     SECURE_MEM_BUDGET >> 20);
    }

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

    // Phase 85: seed the auto-play-videos toggle from the persisted preference.
    media::set_saved_autoplay_enabled(platform::AutoplayPref::default_location().load());

    // Phase 84: seed the gallery view with the persisted preference
    sessions_.gallery.view = platform::GalleryViewPref::default_location().load();

    // Phase 66: seed the warm slot with the persisted default mode
    second_.session.set_default_mode(platform::SecondVaultPref::default_location().load());

    registry_ = platform::VaultRegistry::default_location();
    registry_.seed_if_empty(platform::default_vault_path());
    to_manager();

    // Deliberately carries no phase number: the previous form said "Phase 14"
    // long after Phase 14 shipped, because nothing ever forces a startup string
    // to be updated. A bare marker cannot go stale.
    std::println("[App] Initialised.");
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
    import_ui_.lane.reset();                               // Phase 73: lock stops the lane, reset destroys it
    second_.session.wipe();                                // Phase 66: vault switch locks the warm slot too
    sessions_.adv   = {};                          // new vault session -> fresh advanced search
    sessions_.dual.reset();                        // Phase 78: new vault session -> fresh dual-pane state
    sessions_.gallery.reset();                             // new vault session -> fresh gallery/viewer memory
    // Phase 84: the view is machine-scoped now — a fresh vault session starts
    // in the persisted view, not the enum default.
    sessions_.gallery.view = platform::GalleryViewPref::default_location().load();
    keep_unlocked_ = false;                       // new session always starts with auto-lock on
    vault_state_.active        = std::move(vault_state_.pending);
    vault_state_.active_path   = std::move(vault_state_.pending_path);
    vault_state_.pending_path.clear();
    registry_.add(vault_state_.active_path);                  // move-to-front in the recent list

    // Phase 65: offer the one-time migration. Detection is a pure tree walk
    // (no I/O), so this costs nothing even on a huge vault.
    migration_ui_.offer_open = false;
    migration_ui_.progress_open = false;
    migration_ui_.result_open = false;
    migration_ui_.job.reset();  // reset migration job from previous vault
    if (vault::migration_pending(vault::vault_settings(*vault_state_.active), media::PROBE_CAPS_GEN,
                                 static_cast<uint16_t>(image::THUMB_MAX_SIDE))) {
        // Phase 75: compute thumbs_stale to include thumbnail regen in the scan
        const vault::VaultSettings settings = vault::vault_settings(*vault_state_.active);
        const bool thumbs_stale =
            settings.migrated_thumb_side < static_cast<uint16_t>(image::THUMB_MAX_SIDE);
        const vault::MigrationScan scan = vault::scan_migration(*vault_state_.active, thumbs_stale);
        if (scan.empty()) {
            // Nothing to do: stamp and move on silently, so this vault is never
            // asked again.
            (void)vault::commit_migration(
                *vault_state_.active, vault::stamp_migrated(vault::vault_settings(*vault_state_.active),
                                                             media::PROBE_CAPS_GEN,
                                                             static_cast<uint16_t>(image::THUMB_MAX_SIDE)));
        } else {
            // Guard against import queue race: hold exclusive until outcome is taken
            import_ui_.queue.set_exclusive(true);
            migration_ui_.pending_migration = scan;      // drives the offer modal
            migration_ui_.offer_open = true;
        }
    }

    // Phase 50: Defer import queue start until migration completes (if any).
    // This ensures the migration job has exclusive vault access.
    import_ui_.need_begin_session = true;

    // Phase 73: every interactive commit_index() routes through this lane —
    // serialize on the UI thread, write+fsync on the lane thread. Torn down
    // at every lock site; Vault::lock() itself stops the lane before key wipe.
    import_ui_.lane = std::make_unique<vault::CommitLane>();
    import_ui_.lane->start(*vault_state_.active);
    vault_state_.active->set_commit_router(import_ui_.lane.get());
}

void App::to_gallery(const std::string& path, int selected, bool explicit_index)
{
    state_  = State::Browsing;
    const int seed = explicit_index ? selected : sessions_.gallery.recall(path);
    screen_ = std::make_unique<ui::GalleryGrid>(
        ui::GalleryGrid::GridInitContext{window_, font_, *vault_state_.active, *cache_},
        ui::GalleryGrid::GridDialogs{dialog_, folder_dialog_},
        ui::GalleryGrid::GridVaultCtx{registry_, vault_state_.active_path, &second_.session},
        sessions_.gallery, import_ui_.queue,
        ui::GridLocation{path, seed, sessions_.gallery.view});
    screen_->on_enter();
}

void App::to_dual_gallery()
{
    state_ = State::Browsing;
    // Phase 78: on first entry to split view, seed both panes with the current
    // gallery path. On subsequent visits (via F3 toggle), pane states are
    // preserved in sessions_.dual if has_config is true.
    if (!sessions_.dual.has_config) {
        // Capture the path from the current (single) gallery view before swap.
        // This happens only on first F3 press; subsequent F3 presses restore the
        // saved configuration if has_config is true.
        if (const auto* grid = dynamic_cast<const ui::GalleryGrid*>(screen_.get())) {
            const std::string here = ui::current_gallery_path(*grid);
            sessions_.dual.pane[0].path = here;
            sessions_.dual.pane[1].path = here;
        }
    }
    sessions_.dual.split_active = true;  // entering split view
    screen_ = std::make_unique<ui::DualGalleryScreen>(
        ui::GalleryGrid::GridInitContext{window_, font_, *vault_state_.active, *cache_},
        ui::GalleryGrid::GridDialogs{dialog_, folder_dialog_},
        ui::GalleryGrid::GridVaultCtx{registry_, vault_state_.active_path, &second_.session},
        sessions_.gallery, import_ui_.queue, sessions_.dual);
    screen_->on_enter();
}

void App::enter_viewer(std::unique_ptr<ui::ImageViewer> viewer)
{
    viewer->on_enter();
    ui::apply_video_resume(*viewer, sessions_.gallery);   // resume a matching video, paused
    state_  = State::Viewing;
    screen_ = std::move(viewer);
}

void App::to_viewer(const std::string& gallery_path, int index)
{
    enter_viewer(std::make_unique<ui::ImageViewer>(
        window_, font_, *vault_state_.active, *cache_,
        ui::ImageViewer::Context{folder_dialog_, registry_, import_ui_.queue, vault_state_.active_path, sessions_.gallery.strip_side},
        ui::ImageViewer::Album::gallery(gallery_path), index));
}

void App::to_favorite_images()
{
    state_  = State::Browsing;
    auto screen = std::make_unique<ui::FavoritesImages>(
        window_, font_, *vault_state_.active, *cache_, registry_,
        ui::FavoritesScreen::CollectionOps{dialog_, folder_dialog_, import_ui_.queue,
                                           &second_.session, vault_state_.active_path});
    screen->set_detail_open(sessions_.gallery.detail_open);
    screen_ = std::move(screen);
    screen_->on_enter();
}

void App::to_favorite_galleries()
{
    state_  = State::Browsing;
    auto screen = std::make_unique<ui::FavoritesGalleries>(
        window_, font_, *vault_state_.active, registry_,
        ui::FavoritesScreen::CollectionOps{dialog_, folder_dialog_, import_ui_.queue,
                                           &second_.session, vault_state_.active_path});
    screen->set_detail_open(sessions_.gallery.detail_open);
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
    auto favs = vault::list_favorite_images(*vault_state_.active);
    album.images.reserve(favs.size());
    album.paths.reserve(favs.size());
    for (auto& h : favs) {
        album.images.push_back(h.node);
        album.paths.push_back(std::move(h.path));
    }

    enter_viewer(std::make_unique<ui::ImageViewer>(
        window_, font_, *vault_state_.active, *cache_,
        ui::ImageViewer::Context{folder_dialog_, registry_, import_ui_.queue, vault_state_.active_path, sessions_.gallery.strip_side},
        std::move(album), index));
}

void App::to_advanced_search()
{
    state_  = State::Browsing;
    screen_ = std::make_unique<ui::AdvancedSearchScreen>(
        window_, font_, *vault_state_.active, *cache_, sessions_.adv,
        ui::CollectionBatchOps::Deps{*vault_state_.active, vault_state_.active_path, registry_,
                                     dialog_, window_, &second_.session, folder_dialog_,
                                     import_ui_.queue},
        sessions_.adv.detail_open);
    screen_->on_enter();
}

void App::to_tag_overview()
{
    state_  = State::Browsing;
    screen_ = std::make_unique<ui::TagOverviewScreen>(
        window_, font_, *vault_state_.active, registry_, vault_state_.active_path, dialog_);
    screen_->on_enter();
}

void App::to_tag_galleries(const std::string& tag)
{
    state_  = State::Browsing;
    auto screen = std::make_unique<ui::TagGalleries>(
        window_, font_, *vault_state_.active, registry_, tag,
        ui::FavoritesScreen::CollectionOps{dialog_, folder_dialog_, import_ui_.queue,
                                           &second_.session, vault_state_.active_path});
    screen->set_detail_open(sessions_.gallery.detail_open);
    screen_ = std::move(screen);
    screen_->on_enter();
}

void App::to_tag_images(const std::string& tag)
{
    state_  = State::Browsing;
    auto screen = std::make_unique<ui::TagImages>(
        window_, font_, *vault_state_.active, *cache_, registry_, tag,
        ui::FavoritesScreen::CollectionOps{dialog_, folder_dialog_, import_ui_.queue,
                                           &second_.session, vault_state_.active_path});
    screen->set_detail_open(sessions_.gallery.detail_open);
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
        ui::ImageViewer::Context{folder_dialog_, registry_, import_ui_.queue, vault_state_.active_path, sessions_.gallery.strip_side},
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
    } else if (const auto* dual = dynamic_cast<const ui::DualGalleryScreen*>(screen_.get())) {
        // Phase 78: DualGalleryScreen: return to the active pane's path via ToGallery,
        // which will re-enter split view if split_active is true (set by on_exit).
        (void)dual;
        back = ui::Nav{ToGallery, sessions_.dual.pane[static_cast<std::size_t>(sessions_.dual.active_pane)].path, 0};
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

void App::to_duplicates()
{
    using enum ui::NavKind;
    if (!vault_state_.active) return;

    ui::Nav back{ToGallery, {}, 0};
    if (const auto* grid = dynamic_cast<const ui::GalleryGrid*>(screen_.get())) {
        back = ui::Nav{ToGallery, ui::current_gallery_path(*grid), 0};
    } else if (const auto* dual = dynamic_cast<const ui::DualGalleryScreen*>(screen_.get())) {
        // Phase 78: DualGalleryScreen: return to the active pane's path.
        (void)dual;
        back = ui::Nav{ToGallery, sessions_.dual.pane[static_cast<std::size_t>(sessions_.dual.active_pane)].path, 0};
    }

    state_  = State::Browsing;
    screen_ = std::make_unique<ui::DuplicatesScreen>(
        window_, font_, *vault_state_.active, *cache_, back);
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

// Phase 66: corner badge shown while a SECOND vault's key is in memory. Never
// fades (unlike the Phase 45 keep-unlocked badge): visibility must match key
// lifetime exactly. Stacks above the keep-unlocked badge when both are shown.
void draw_second_vault_badge(gfx::Renderer& r, gfx::FontAtlas& font, int win_w, int win_h,
                             bool stacked)
{
    using namespace gfx::theme;
    const auto st = ui::second_vault_status();
    std::string label =
        st.mode == platform::SecondVaultMode::KeepSession
            ? std::format("2nd vault unlocked · session — {}",
                          platform::path_to_utf8(platform::utf8_to_path(st.path).stem()))
            : std::format("2nd vault unlocked · {} — {}", ui::format_keep_open_left(st.seconds_left),
                          platform::path_to_utf8(platform::utf8_to_path(st.path).stem()));
    static constexpr auto PAD    = 10.0f;
    static constexpr auto MARGIN = 16.0f;
    const auto max_w = static_cast<float>(win_w) * 0.6f;
    if (const auto measured_w = static_cast<float>(font.measure(label)); measured_w > max_w) {
        label = ui::fit_text(font, label, max_w);
    }
    const auto  tw = static_cast<float>(font.measure(label));
    const float th = font.pixel_height();
    const float bw = tw + (PAD * 2);
    const float bh = th + (PAD * 2);
    const float lift = stacked ? bh + 8.0f : 0.0f;   // clear the keep-unlocked badge
    const SDL_FRect box{static_cast<float>(win_w) - bw - MARGIN,
                        static_cast<float>(win_h) - bh - MARGIN - lift, bw, bh};
    r.draw_round_rect(box, RADIUS_SMALL, SURFACE);
    r.draw_round_rect(box, RADIUS_SMALL, WARN, /*filled*/ false);
    r.draw_text(font, box.x + PAD, box.y + PAD, label, WARN);
}

// Phase 65 migration modals. Split out of render_frame so the frame path stays a
// dispatcher instead of a wall of panel layout. Each veils the whole window: the
// migration owns the vault exclusively while it runs, so nothing behind it is
// safe to interact with.
void draw_migration_offer(gfx::Renderer& r, gfx::FontAtlas& font, float win_w, float win_h,
                          const vault::MigrationScan& scan)
{
    r.draw_rect({0, 0, win_w, win_h}, gfx::Color{8, 9, 12, 255});
    const float pw = 600;
    const float ph = 300;
    const float px = (win_w - pw) / 2;
    const float py = (win_h - ph) / 2;
    r.draw_round_rect({px, py, pw, ph}, gfx::theme::RADIUS, gfx::theme::SURFACE);
    r.draw_round_rect({px, py, pw, ph}, gfx::theme::RADIUS, gfx::theme::ACCENT, false);

    r.draw_text(font, px + 20, py + 20, "Vault upgrade available", gfx::theme::TEXT);

    float       text_y = py + 60;
    const float line_h = 20;

    // Phase 75: include thumbnail count in the offer message
    std::string summary;
    if (scan.thumbs > 0) {
        summary = std::format("This vault has {} thumbnail(s), {} video(s), and {} image(s)",
                              scan.thumbs, scan.videos, scan.images);
    } else {
        summary = std::format("This vault has {} video(s) and {} image(s)", scan.videos, scan.images);
    }
    r.draw_text(font, px + 20, text_y, summary, gfx::theme::TEXT);
    text_y += line_h;

    if (scan.thumbs > 0) {
        r.draw_text(font, px + 20, text_y, "with thumbnails to sharpen, and videos/images",
                    gfx::theme::TEXT);
        text_y += line_h;
        r.draw_text(font, px + 20, text_y,
                    "imported before this build could read them fully.", gfx::theme::TEXT);
    } else {
        r.draw_text(font, px + 20, text_y,
                    "that were imported before this build could read them fully.", gfx::theme::TEXT);
    }
    text_y += line_h;
    r.draw_text(font, px + 20, text_y,
                std::format("Upgrading reads {} and rewrites the vault once.",
                            ui::format_size(scan.bytes)),
                gfx::theme::TEXT);
    text_y += line_h;
    r.draw_text(font, px + 20, text_y, "Unused space is reclaimed.", gfx::theme::TEXT);
    text_y += line_h + 10;
    r.draw_text(font, px + 20, text_y,
                "The app is unusable while this runs. You can cancel at any time.",
                gfx::theme::TEXT);

    r.draw_text(font, px + 20, py + ph - 30, "[ Upgrade now (Y) ]  [ Not now (N) ]",
                gfx::theme::TEXT_DIM);
}

void draw_migration_progress(gfx::Renderer& r, gfx::FontAtlas& font, float win_w, float win_h,
                             const ui::MigrationJob& job)
{
    const int total = job.total();
    const int done  = job.done();
    const ui::MigrationProgressText text = ui::migration_progress_text(job.phase(), done, total);
    ui::draw_op_progress(r, font, win_w, win_h,
                         {.title = text.title, .count_line = text.count_line,
                          .done = done, .total = total});
}

void draw_migration_result(gfx::Renderer& r, gfx::FontAtlas& font, float win_w, float win_h,
                           const ui::MigrationOutcome& res)
{
    r.draw_rect({0, 0, win_w, win_h}, gfx::Color{8, 9, 12, 255});
    const float pw = 560;
    const float ph = 320;
    const float px = (win_w - pw) / 2;
    const float py = (win_h - ph) / 2;
    r.draw_round_rect({px, py, pw, ph}, gfx::theme::RADIUS, gfx::theme::SURFACE);
    r.draw_round_rect({px, py, pw, ph}, gfx::theme::RADIUS,
                      res.ok ? gfx::theme::ACCENT : gfx::theme::DANGER, false);

    r.draw_text(font, px + 20, py + 20, res.ok ? "Upgrade complete" : "Upgrade failed",
                gfx::theme::TEXT);

    float       text_y = py + 60;
    const float line_h = 20;
    if (res.cancelled) {
        r.draw_text(font, px + 20, text_y, "Cancelled. Retry at next unlock.", gfx::theme::TEXT);
    } else if (res.ok) {
        for (const std::string& line :
             {std::format("Fixed {} video(s)", res.videos_fixed),
              std::format("Fixed {} image(s)", res.images_fixed),
              std::format("Skipped {} video(s)", res.videos_skipped),
              std::format("Reclaimed {}", ui::format_size(res.reclaimed_bytes))}) {
            r.draw_text(font, px + 20, text_y, line, gfx::theme::TEXT);
            text_y += line_h;
        }
    } else {
        r.draw_text(font, px + 20, text_y, std::format("Error: {}", res.error),
                    gfx::theme::DANGER);
    }

    r.draw_text(font, px + 20, py + ph - 30, "[Enter or any key to continue]",
                gfx::theme::TEXT_DIM);
}
} // namespace

struct App::OverlayDispatch {
    static bool help(App& app, const SDL_Event& e)
    {
        // F1 toggles help (checked before the help.open guard so it opens/closes
        // over settings).
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F1) {
            ui::toggle_help(app.overlays_.help);
            return true;
        }
        // Help popup (highest priority: swallows arrow/wheel; over settings)
        if (!app.overlays_.help.open) return false;
        if (e.type == SDL_EVENT_KEY_DOWN) {
            ui::handle_help_key(app.overlays_.help, e.key.key);
        } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            ui::handle_help_wheel(app.overlays_.help, e.wheel.y);
        }
        return true;
    }

    static bool settings(App& app, const SDL_Event& e)
    {
        // F2 toggles settings
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F2) {
            if (app.overlays_.settings.open) {
                ui::close_settings(app.overlays_.settings, app.window_);
            } else {
                app.open_settings_overlay();
            }
            return true;
        }
        // Phase 65: manual migration trigger from the VaultOps section, handled
        // before the settings panel swallows input. On a non-empty scan this
        // deliberately does NOT report the event as handled: settings closes and the
        // offer modal must see this same event, so it renders and takes input on
        // this frame instead of the next one.
        if (app.overlays_.settings.open && app.overlays_.settings.trigger_migration &&
            app.vault_state_.active) {
            app.overlays_.settings.trigger_migration = false;

            // Scan for actual pending work regardless of watermark state
            // Phase 75: include thumbnail regen in the scan
            const vault::VaultSettings settings = vault::vault_settings(*app.vault_state_.active);
            const bool thumbs_stale =
                settings.migrated_thumb_side < static_cast<uint16_t>(image::THUMB_MAX_SIDE);
            const vault::MigrationScan scan = vault::scan_migration(*app.vault_state_.active, thumbs_stale);
            if (scan.empty()) {
                // Nothing to do: inform the user and keep settings open
                app.overlays_.settings.error = "Nothing to upgrade";
                return true;
            }
            ui::close_settings(app.overlays_.settings, app.window_);
            app.import_ui_.queue.set_exclusive(true);   // guard against an import race
            app.migration_ui_.pending_migration = scan;
            app.migration_ui_.offer_open        = true;
        }
        // Settings panel (second priority: swallows all events)
        if (!app.overlays_.settings.open) return false;
        if (bool commit = false; ui::handle_settings_event(app.overlays_.settings, app.window_, e, commit)) {
            // Phase 66: sync the default mode whenever the event was handled
            app.second_.session.set_default_mode(app.overlays_.settings.second_vault_default);
            // Phase 84: sync gallery view to session and live grid (if one is open behind overlay)
            app.sessions_.gallery.view = app.overlays_.settings.gallery_view;
            if (auto* grid = dynamic_cast<ui::GalleryGrid*>(app.screen_.get())) {
                ui::set_gallery_view(*grid, app.overlays_.settings.gallery_view);
            }
            // Commit vault settings if the commit flag was set
            if (commit && app.overlays_.settings.vault_unlocked && app.vault_state_.active &&
                vault::set_vault_settings(*app.vault_state_.active, app.overlays_.settings.draft) !=
                    vault::VaultResult::Ok) {
                app.overlays_.settings.error = "Could not save settings";
            }
        }
        return true;
    }

    // Enter/Y starts the job; Esc/N dismisses it for this session (it is re-offered
    // at the next unlock). Any other key is ignored — the caller still swallows it.
    static void offer_key(App& app, SDL_Keycode key)
    {
        if (key == SDLK_RETURN || key == SDLK_Y) {
            // "Upgrade now" — start the migration job
            if (!app.vault_state_.active) return;
            if (!app.migration_ui_.job)
                app.migration_ui_.job = std::make_unique<ui::MigrationJob>();
            if (app.migration_ui_.job->start(*app.vault_state_.active)) {
                app.migration_ui_.offer_open    = false;
                app.migration_ui_.progress_open = true;
            }
        } else if (key == SDLK_ESCAPE || key == SDLK_N) {
            // "Not now" — dismiss for this session, re-offer at next unlock
            app.migration_ui_.offer_open = false;
            // Release exclusivity: the migration was never started
            app.import_ui_.queue.set_exclusive(false);
        }
    }

    // Phase 65 modals, in priority order: result > progress > offer. Each swallows
    // every event while it is up — the job owns the vault exclusively while it runs.
    static bool migration(App& app, const SDL_Event& e)
    {
        if (app.migration_ui_.result_open) {
            if (e.type == SDL_EVENT_KEY_DOWN) app.migration_ui_.result_open = false;
            return true;
        }
        if (app.migration_ui_.progress_open) {
            // Only Esc does anything: it asks the job to stop between items.
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE && app.migration_ui_.job)
                app.migration_ui_.job->cancel();
            return true;
        }
        if (!app.migration_ui_.offer_open) return false;
        if (e.type == SDL_EVENT_KEY_DOWN) offer_key(app, e.key.key);
        return true;
    }

    // Lock-confirm modal (lowest priority: key events only; Phase 50)
    static bool lock_confirm(App& app, const SDL_Event& e)
    {
        if (!app.import_ui_.lock_confirm.open) return false;
        if (e.type == SDL_EVENT_KEY_DOWN) {
            using enum ui::LockConfirmKey;
            const auto key = ui::classify_lock_confirm_key(e.key.key);
            if (key == Confirm) {
                app.import_ui_.queue.abort_and_flush();
                app.import_ui_.replay_nav   = app.import_ui_.lock_confirm.action;
                app.import_ui_.lock_confirm = {};
            } else if (key == Cancel) {
                app.import_ui_.lock_confirm = {};
            }
        }
        return true;
    }
};

bool App::dispatch_overlay_event(App& app, const SDL_Event& e)
{
    using D = OverlayDispatch;
    if (D::help(app, e)) return true;
    if (D::settings(app, e)) return true;
    if (D::migration(app, e)) return true;
    return D::lock_confirm(app, e);
}

void App::dispatch_event(const SDL_Event& e)
{
    if (is_user_input(e)) idle_.reset();
    // Phase 56: right-click is a universal "back". Translate it here, once, so
    // every screen and modal reuses its own Esc handling instead of growing a
    // parallel cancel path. The release is dropped — a surface that never saw
    // the press must not see a dangling release.
    if (is_back_click_release(e)) return;
    if (is_back_click(e)) {
        dispatch_event(make_back_key_event());
        return;
    }
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
        while (window_.poll_event(e)) {
            // Phase 56: SDL reports mouse positions in window points; every layout in
            // this app is in render pixels. Convert once, here, before any consumer.
            gfx::scale_mouse_event(e, window_.pixel_density());
            dispatch_event(e);
            should_redraw = true;
        }
    } else if (SDL_WaitEventTimeout(&e, IDLE_HEARTBEAT_MS)) {
        // Idle: block until an event (or the heartbeat) rather than spinning.
        gfx::scale_mouse_event(e, window_.pixel_density());
        dispatch_event(e);
        should_redraw = true;
        while (window_.poll_event(e)) {
            gfx::scale_mouse_event(e, window_.pixel_density());
            dispatch_event(e);
        }
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
    // sessions_.gallery before it is destroyed (Phase 39 Part 2) — must run before
    // on_exit(), which tears down ImageViewer's live video_.
    if (const auto* grid = dynamic_cast<const ui::GalleryGrid*>(screen_.get())) {
        sessions_.gallery.view = ui::current_gallery_view(*grid);
    } else if (const auto* viewer = dynamic_cast<const ui::ImageViewer*>(screen_.get())) {
        sessions_.gallery.strip_side = ui::current_strip_side(*viewer);
        ui::capture_video_resume(*viewer, sessions_.gallery);
    } else if (const auto* fav = dynamic_cast<const ui::FavoritesScreen*>(screen_.get())) {
        sessions_.gallery.detail_open = ui::current_detail_open(*fav);
    } else if (const auto* adv = dynamic_cast<const ui::AdvancedSearchScreen*>(screen_.get())) {
        sessions_.adv.detail_open = ui::current_detail_open(*adv);
    } else if (const auto* dual = dynamic_cast<const ui::DualGalleryScreen*>(screen_.get())) {
        // Phase 78: DualGalleryScreen::on_exit() already snapshots both panes into
        // sessions_.dual; no additional capture needed here.
        (void)dual;  // explicitly unused for -Wunused-parameter
    }
}

void App::open_settings_overlay()
{
    overlays_.settings.vault_unlocked = vault_state_.active && vault_state_.active->is_unlocked();
    overlays_.settings.draft = overlays_.settings.vault_unlocked ? vault::vault_settings(*vault_state_.active)
                                                                  : vault::VaultSettings{};
    overlays_.settings.theme = gfx::active_theme_id();
    overlays_.settings.gallery_view = sessions_.gallery.view;
    overlays_.settings.second_vault_default = second_.session.default_mode();   // Phase 66
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

    // Every transition below except ToggleKeepUnlocked/ToSettings/LockSecond/Quit/None destroys the
    // current screen.
    if (nav.kind != None && nav.kind != ToggleKeepUnlocked && nav.kind != ToSettings &&
        nav.kind != LockSecond && nav.kind != Quit) {
        capture_session_state();
        screen_->on_exit();
    }
    switch (nav.kind) {
        case ToGallery:
            if (state_ == State::Locked) promote_pending();   // unlock-screen success
            // Phase 78: viewer round-trip back to split view. If the viewer was
            // launched from a dual-pane screen and split is still active, restore
            // that pane's exact position instead of going to single-grid mode.
            if (from_viewer && sessions_.dual.split_active) {
                sessions_.dual.pane[static_cast<std::size_t>(sessions_.dual.active_pane)].path = nav.path;
                sessions_.dual.pane[static_cast<std::size_t>(sessions_.dual.active_pane)].selected = nav.index;
                to_dual_gallery();
                return true;
            }
            if (vault_state_.active) to_gallery(nav.path, nav.index, from_viewer);
            else         to_manager();                        // defensive: nothing unlocked
            return true;
        case ToDualGallery:
            to_dual_gallery();
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
        case ToDuplicates:        to_duplicates(); return true;
        case ToUnlock:
            import_ui_.queue.end_session();          // Phase 50: flush before switch
            import_ui_.lane.reset();                 // Phase 73: reset CommitLane after session
            // Phase 66: switching to the warm vault promotes its already-unlocked
            // handle — no password prompt. take() empties the slot; promote_pending
            // then locks the old active and runs the standard new-session resets.
            if (second_.session.occupied() && second_.session.path() == nav.path) {
                vault_state_.pending      = std::make_unique<vault::Vault>(second_.session.take());
                vault_state_.pending_path = nav.path;
                promote_pending();
                to_gallery();
                return true;
            }
            to_unlock(nav.path);
            return true;
        case ToVaultManager:      vault_state_.pending.reset(); to_manager();      return true;
        case LockActive:
            keep_unlocked_ = false;
            second_.session.wipe();                          // Phase 66: locking up means locking everything
            sessions_.dual.reset();                // Phase 78: fresh dual-pane state on lock
            sessions_.gallery.reset();                     // Phase 39 Part 2: fresh session on lock
            import_ui_.queue.end_session();          // Phase 50: flush before lock
            if (vault_state_.active) {
                vault_state_.active->lock();                // Phase 73: lock stops the lane
                import_ui_.lane.reset();                    // then destroy it
                vault_state_.active.reset();
                vault_state_.active_path.clear();
            }
            to_manager();
            return true;
        case LockSecond:
            // Phase 66: explicit "lock now" on the warm slot (vault manager).
            second_.session.wipe();
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
    // "keep unlocked" toggle (Phase 33), a busy import queue (Phase 50), and a
    // running vault upgrade (Phase 79 — MigrationJob owns the vault exclusively;
    // a >5-min-idle upgrade must not have it locked and destroyed mid-flight) —
    // see app/auto_lock.h.
    const bool migration_active = migration_ui_.job && migration_ui_.job->active();
    if (const bool blocks = screen_ && screen_->blocks_idle_lock();
        !should_auto_lock(vault_state_.active != nullptr, blocks, keep_unlocked_, import_ui_.queue.busy(),
                          migration_active, idle_, dt))
        return false;
    // The upgrade OFFER modal is a different story: the job has not started, so
    // locking an unattended vault is the right default. Drop the offer's state
    // with it — the modal must not linger over the manager referencing a locked
    // vault (it is re-offered at the next unlock), and the import-queue
    // exclusivity it took must not leak into the locked session.
    if (migration_ui_.offer_open) {
        migration_ui_.offer_open = false;
        import_ui_.queue.set_exclusive(false);
    }
    if (screen_) screen_->on_exit();
    sessions_.dual.reset();                       // Phase 78: fresh dual-pane state on idle lock
    sessions_.gallery.reset();                                  // Phase 39 Part 2: fresh session on idle lock
    import_ui_.queue.end_session();                       // Phase 50: flush before lock
    vault_state_.active->lock();                       // Phase 73: lock stops the lane
    import_ui_.lane.reset();                           // then destroy it
    vault_state_.active.reset();
    vault_state_.active_path.clear();
    to_manager();
    std::println("[App] Auto-locked after {} s idle.", static_cast<int>(IDLE_LOCK_SECS));
    return true;
}

void App::update(double dt)
{
    const platform::PerfScope perf("app.update", 10.0);
    if (screen_) screen_->update(dt);
    badge_elapsed_ += dt;   // Phase 45 Part 6

    // Phase 66: tick the warm slot. Expiry is deferred while a background job
    // owns a vault handle (same signals that suppress the idle auto-lock).
    {
        const bool defer = (screen_ && screen_->blocks_idle_lock()) || import_ui_.queue.busy();
        const bool expired = second_.session.tick(dt, defer);
        const int  secs    = second_.session.occupied() ? static_cast<int>(second_.session.seconds_left()) : -1;
        if ((expired || secs != second_.badge_secs) && screen_) screen_->mark_dirty();
        second_.badge_secs = secs;
    }

    // Phase 65: handle migration job outcome and start import session after migration.
    // Phase 79: the outcome poll must NOT be gated on vault_state_.active — none of
    // it needs the vault, and if the vault ever goes away under a live job (any
    // future teardown path), a gated poll leaves job->active() true forever and the
    // progress modal wedges on screen with no way to dismiss it.
    if (migration_ui_.job && migration_ui_.job->active()) {
        // Migration is running; collect outcome if it just finished
        if (auto outcome = migration_ui_.job->take_outcome()) {
            migration_ui_.result = *outcome;
            migration_ui_.progress_open = false;
            migration_ui_.result_open = true;

            // MigrationJob owns watermark stamping and commits it in its finalization phase.
            // Do not duplicate the commit here; it would append an index blob after compaction.

            // Release exclusive hold on import queue (success, cancel, or error)
            import_ui_.queue.set_exclusive(false);
        }
    }

    // Start import session once migration is done (or skipped)
    if (vault_state_.active && import_ui_.need_begin_session && !migration_ui_.offer_open &&
        !migration_ui_.progress_open && (!migration_ui_.job || !migration_ui_.job->active())) {
        import_ui_.need_begin_session = false;
        // Phase 73: App-owned lane was started in promote_pending; pass to ImportQueue
        import_ui_.queue.begin_session(*vault_state_.active, *import_ui_.lane);
    }

    // Phase 50: drain the import queue and refresh screens when records are applied
    if (vault_state_.active && import_ui_.queue.drain(dt) > 0 && screen_) {
        screen_->on_vault_changed();
        screen_->mark_dirty();
    }
}

void App::render_frame()
{
    const platform::PerfScope perf("frame", 20.0);
    const uint64_t render_start = SDL_GetTicksNS();
    window_.begin_frame(gfx::theme::BG.r, gfx::theme::BG.g, gfx::theme::BG.b);
    if (screen_) {
        gfx::Renderer r(window_.sdl_renderer());
        screen_->render(r);
        const bool keep_badge = vault_state_.active && should_show_badge(keep_unlocked_, badge_elapsed_, BADGE_WINDOW_SECS);
        if (keep_badge)
            draw_keep_unlocked_badge(r, font_, window_.width(), window_.height());
        if (second_.session.occupied())
            draw_second_vault_badge(r, font_, window_.width(), window_.height(), keep_badge);
        if (overlays_.settings.open) {
            ui::draw_settings_overlay(r, font_, static_cast<float>(window_.width()),
                                      static_cast<float>(window_.height()), overlays_.settings);
        }

        // Phase 65: render migration modals (offer > progress > result)
        const auto w = static_cast<float>(window_.width());
        const auto h = static_cast<float>(window_.height());
        if (migration_ui_.offer_open) {
            draw_migration_offer(r, font_, w, h, migration_ui_.pending_migration);
        } else if (migration_ui_.progress_open && migration_ui_.job &&
                   migration_ui_.job->active()) {
            draw_migration_progress(r, font_, w, h, *migration_ui_.job);
        } else if (migration_ui_.result_open) {
            draw_migration_result(r, font_, w, h, migration_ui_.result);
        }

        // Phase 50: render lock_confirm modal after migration/settings so it stays on top
        if (import_ui_.lock_confirm.open) {
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

        update(dt);

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
    // Phase 79: closing the window mid-upgrade reaches here with the coordinator
    // still holding a reference to the active vault — stop it BEFORE any vault
    // teardown (blocking, acceptable at shutdown; cancel commits applied work
    // and leaves the watermark unstamped, so the upgrade is re-offered).
    if (migration_ui_.job) migration_ui_.job->abort_and_join();
    if (screen_) { screen_->on_exit(); screen_.reset(); }
    import_ui_.queue.end_session();        // Phase 50: flush before lock (blocking, acceptable at shutdown)
    second_.session.wipe();                        // Phase 66: wipe warm slot before vault teardown
    if (vault_state_.active) {
        vault_state_.active->lock();               // Phase 73: lock stops the lane
        import_ui_.lane.reset();                   // then destroy it
    }
    if (vault_state_.pending) vault_state_.pending->lock();
    vault_state_.active.reset();
    vault_state_.pending.reset();
    if (cache_) cache_->clear();        // destroy thumbnail textures before the renderer
    font_.release_texture();
    window_.shutdown();
    std::println("[App] Clean shutdown.");
}

} // namespace app
