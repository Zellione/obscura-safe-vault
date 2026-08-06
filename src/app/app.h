#pragma once

#include <memory>
#include <string>

#include "app/idle_timer.h"
#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/folder_dialog.h"
#include "platform/vault_registry.h"
#include "ui/advanced_search_state.h"
#include "ui/gallery_session_state.h"
#include "ui/help_popup.h"
#include "ui/import_queue.h"
#include "ui/screen.h"
#include "ui/second_vault.h"
#include "ui/settings_model.h"
#include "ui/migration_job.h"
#include "vault/vault.h"
#include "vault/migration.h"

namespace ui { class ImageViewer; }

namespace app {

enum class State { Locked, Managing, Browsing, Viewing };

class App {
public:
    App()  = default;
    ~App() = default;

    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    [[nodiscard]] bool init();
    void run();
    void shutdown();

private:
    void to_manager();
    void to_unlock(const std::string& path);
    // `explicit_index` is true only when `selected` is a real, freshly-known
    // position (currently: the viewer returning to its exact launch position)
    // — otherwise `selected` is ignored in favor of session_.recall(path), the
    // last-remembered tile at that path (Phase 40 Part 2).
    void to_gallery(const std::string& path = {}, int selected = 0, bool explicit_index = false);
    void to_viewer(const std::string& gallery_path, int index);
    void to_favorite_images();
    void to_favorite_galleries();
    void to_favorite_viewer(int index);   // viewer over the whole favorites set
    void to_advanced_search();            // Phase 18 advanced-search screen
    void to_tag_overview();               // Phase 22 tag-overview screen
    void to_tag_galleries(const std::string& tag);   // galleries directly carrying `tag`
    void to_tag_images(const std::string& tag);            // images directly carrying a tag
    void to_tag_viewer(const std::string& tag, int index); // viewer over a tag's media set
    void to_import_status();              // Phase 50: import status screen
    void to_duplicates();                 // Duplicate finder screen
    void promote_pending();               // unlock success: pending_ -> active_ (locks old)

    // Shared tail of every viewer-construction site (Phase 39 Part 2): enters the
    // viewer, applies a matching video-resume bookmark, then activates it.
    void enter_viewer(std::unique_ptr<ui::ImageViewer> viewer);
    // Snapshots the outgoing screen's view/strip-side/video-position into
    // session_, if it is a GalleryGrid or ImageViewer (Phase 39 Part 2). Called
    // right before a screen is torn down by a nav transition.
    void capture_session_state();

    // run() helpers (kept small so the loop stays readable).
    void dispatch_event(const SDL_Event& e);     // quit/close here, else to screen
    bool pump_events(bool animating);            // wait/poll + dispatch; should redraw?
    bool apply_nav();                            // resolve a transition; transitioned?
    bool maybe_auto_lock(double dt);             // idle -> wipe active_, return to manager
    void update(double dt);                      // update frame state and drain import queue
    void render_frame();                         // draw + present + frame-cap fallback
    void open_settings_overlay();                // seed settings state and open the overlay

    // Dispatch event to overlays (help > settings > migration > lock_confirm);
    // true if handled.
    static bool dispatch_overlay_event(App& app, const SDL_Event& e);

    // The per-overlay handlers dispatch_overlay_event fans out to, defined in
    // app.cpp. A nested type rather than more App members: it reaches App's
    // privates exactly as a member would, without growing App's own interface
    // for what is one function's worth of internal structure.
    struct OverlayDispatch;

    gfx::Window                        window_;
    gfx::FontAtlas                     font_;
    bool                               font_ready_ = false;
    std::unique_ptr<gfx::TextureCache> cache_;
    platform::FileDialog               dialog_;
    platform::FolderDialog             folder_dialog_;
    platform::VaultRegistry            registry_;
    std::unique_ptr<ui::Screen>        screen_;

    struct VaultState {
        std::unique_ptr<vault::Vault> active;         // the single unlocked vault
        std::string                   active_path;
        std::unique_ptr<vault::Vault> pending;        // vault being unlocked right now
        std::string                   pending_path;
    };
    VaultState                         vault_state_;
    // Phase 66: the one warm cross-vault destination (path + unlocked Vault +
    // mode + sliding deadline). Session-only; App ticks/wipes it; dialogs adopt
    // into it via GridVaultCtx. Declared next to vault_state_ so its ~Vault
    // backstop runs in the same destruction neighborhood as the active vault.
    struct SecondVaultUi {
        ui::SecondVaultSession session;
        int badge_secs = -1;  // last drawn countdown second
    };
    SecondVaultUi                      second_;
    // Advanced-search state preserved across visits within one unlocked-vault
    // session; reset in promote_pending() whenever the active vault changes.
    ui::AdvancedSearchState            adv_session_;
    // Gallery/viewer session state (Phase 39 Part 2): last-used List/Grid view +
    // thumbnail-strip side, plus a single "last video watched" resume bookmark;
    // carried through App's screen reconstruction on every grid<->viewer round
    // trip. Reset at the same points adv_session_ is (LockActive, idle auto-lock,
    // vault switch).
    ui::GallerySessionState             session_;
    State                              state_   = State::Locked;
    bool                               running_ = false;   // main-loop run flag

    // Idle auto-lock: wipe the active vault's key after this much inactivity and
    // return to the manager (single-active; compile-time default, spec §2.2).
    static constexpr double            IDLE_LOCK_SECS = 5 * 60.0;
    IdleTimer                          idle_{IDLE_LOCK_SECS};

    // Session-only opt-out of the idle auto-lock (Phase 33), toggled from
    // GalleryGrid (`U`). Never persisted; always resets to false whenever the
    // active vault changes (promote_pending) or is explicitly locked
    // (LockActive) — re-unlocking always starts with auto-lock on.
    bool                               keep_unlocked_ = false;

    // Phase 45 Part 6: seconds since the last `U` press that left
    // keep_unlocked_ true; the badge is visible only while this is under
    // BADGE_WINDOW_SECS. Starts large so no badge shows before any toggle.
    static constexpr double            BADGE_WINDOW_SECS = 10.0;
    double                             badge_elapsed_ = BADGE_WINDOW_SECS;

    // The two global overlays App owns and intercepts a function key for.
    struct Overlays {
        ui::HelpPopupState help;       // Phase 39: F1 help popup
        ui::SettingsState  settings;   // Phase 49: F2 settings overlay
    };
    Overlays                           overlays_;

    // Phase 65: unlock-time migration. Sequenced carefully to ensure the job has
    // exclusive vault access: migration is offered and run BEFORE the import queue
    // begins_session (see promote_pending and update).
    struct MigrationUi {
        vault::MigrationScan pending_migration;  // detected at unlock; triggers offer modal
        std::unique_ptr<ui::MigrationJob> job;  // the running coordinator (ptr to avoid deleted ops)
        bool                 offer_open = false; // offer modal is showing
        bool                 progress_open = false;  // progress modal is showing
        bool                 result_open = false;    // result summary is showing
        ui::MigrationOutcome result;             // handed back by take_outcome()
    };
    MigrationUi                        migration_ui_;

    // Phase 50: import queue and related UI state. Declared after active_/pending_
    // so ~ImportQueue (which flushes into the vault) runs before the vault is destroyed.
    struct ImportUi {
        // A lock-ish action (LockActive / ToUnlock / Quit / manager switch) requested
        // while imports are pending. The action is parked here behind a default-cancel
        // confirm modal; Y aborts the queue then replays the Nav, N/Esc discards it.
        struct PendingLockConfirm { bool open = false; ui::Nav action; };
        PendingLockConfirm lock_confirm;

        // Nav to be processed on the next apply_nav() call (set by dispatch_event
        // when the user confirms a parked lock action).
        ui::Nav replay_nav;

        // Background import queue.
        ui::ImportQueue queue;

        // True when import session has not yet begun (deferred until after
        // migration completes at unlock). Set by promote_pending, consumed by update.
        bool need_begin_session = false;
    };
    ImportUi                           import_ui_;
};

} // namespace app
