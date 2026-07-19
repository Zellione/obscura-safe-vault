# Module: app/ + platform/ — lifecycle & OS integration

Referenced from `mem:core`. Covers `src/app/` (state machine + event loop) and
`src/platform/` (config dirs, dialogs, persistence, hardening).

## app/ — main.cpp, app.{h,cpp}
- State machine + SDL event loop. Event-driven: blocks on `SDL_WaitEventTimeout` when idle,
  renders only on input / async result / animation (`Screen::animating()` +
  `mark_dirty`/`consume_dirty`), so the GPU idles instead of free-running. VSync on
  (`Window::vsync()`); manual ~60fps cap fallback when VSync is unavailable.
- VaultManager is the home screen. App owns ONE unlocked vault (`active_`) at a time + a
  transient `pending_` during unlock. Single-active / lock-on-switch: opening another vault
  wipes the old key; shutdown wipes both. `promote_pending()` runs only on unlock success
  (ToGallery while state==Locked).

### Idle auto-lock
- `app/idle_timer.h` `IdleTimer` (reset on user input in `dispatch_event`);
  `maybe_auto_lock(dt)` wipes `active_` + `to_manager()` after `IDLE_LOCK_SECS = 5 min`.
- `app/auto_lock.h` `should_auto_lock(has_active, blocks_idle_lock, keep_unlocked, timer, dt)`
  — pure, unit-tested extraction of the 3 suppression guards (no active vault / a screen's
  `blocks_idle_lock` / the session-only `keep_unlocked_` toggle), all of which reset the
  IdleTimer instead of ticking it.
- `keep_unlocked_` is a plain App bool, flipped by GalleryGrid's `U` via
  `NavKind::ToggleKeepUnlocked` (`App::apply_nav` flips it in place — no screen swap); reset
  to false in `promote_pending()` + the LockActive nav case, so re-unlocking always starts
  with auto-lock ON. `App::render_frame` draws a corner badge ("Auto-lock off [U]",
  `draw_keep_unlocked_badge`) over whatever screen is active whenever `active_ &&
  keep_unlocked_` — an App-level overlay, not per-screen.

### Session state
- App owns `ui::GallerySessionState session_` (mirrors `adv_session_`): last GalleryGrid view
  density (List/GridS/GridM/GridL/GridXL) + ImageViewer strip side + a single "last video
  watched" resume bookmark, carried across App's screen reconstruction on every nav
  transition. `capture_session_state()` (dynamic_cast onto the active Screen) snapshots it
  right before `on_exit()`; `to_gallery`/`to_viewer`/`to_favorite_viewer`/`to_tag_viewer` feed
  `session_.view`/`strip_side` back in as the new screen's initial ctor arg. `enter_viewer()`
  is the shared tail of every ImageViewer construction: `on_enter()` then
  `ui::apply_video_resume()`. Reset (`session_.reset()`) at LockActive, idle auto-lock, and
  promote_pending.
- App also owns `HelpPopupState` (intercepts F1 globally, renders the overlay on top) and
  `AdvancedSearchState adv_session_` (reset on vault change).

## platform/
- `paths.*`, `file_dialog.*` — config dirs, SDL file dialogs (`save_vault()`). Each open is
  tagged with a `Purpose` + `take_result(Purpose)` so one shared dialog polled by two handlers
  (image pick vs zip import) can't steal each other's result. `Purpose::TagList` +
  `open_tag_list()` (.txt); `open_zip()`'s filter accepts `zip;cbz`. Externally-supplied paths
  (dialog results, `vaults.list` lines) go through `platform::normalize_user_path` before they
  reach `fopen`.
- `folder_dialog.*` — export destination picker.
- `vault_registry.*` — recent-vaults list: config-dir file of known vault PATHS ONLY (no
  secrets); `list`/`add`(move-to-front,dedup)/`remove`/`seed_if_empty`; atomic temp+rename.
- `theme_pref.*` — chosen UI theme persistence: `config_dir()/theme.conf` holds the theme's
  stable slug ONLY (no secrets); `load()`->ThemeId (missing/unknown -> default), `save(id)`;
  atomic temp+rename (mirrors vault_registry). Loaded in `App::init()`, saved live by
  ThemePicker.
- `VolumePref` — `config_dir()/volume.conf`, one float [0,1], atomic write, missing/invalid
  -> 1.0; App loads at init + saves on clean exit (the in-memory global lives in
  `media/volume_setting.*`, not AV-gated).
- `harden.{h,cpp}` — `disable_core_dumps()`: `prctl(PR_SET_DUMPABLE,0)` +
  `setrlimit(RLIMIT_CORE,{0,0})` on Linux, no-op on Windows (macOS removed — `#error` guard in
  `src/crypto/random.cpp`); called once at app init, Release (NDEBUG) builds only, before any
  vault unlock, to keep decrypted data/keys out of core dumps. Also
  `redirect_stream_to_file`/`redirect_diagnostics_to_log_file` (Windows Release only — a
  windowless WindowedApp process has no valid stdout/stderr handle, so every
  `std::println(stderr,...)` would throw `std::system_error` and terminate(); redirects both to
  `config_dir()/console.log`).
- `error_log.*` — persistent best-effort error log: `log_error(tag,msg)` appends `[tag] msg`
  to stderr + `config_dir()/error.log`. `install_terminate_logger()` installs
  `std::set_terminate` so an uncaught exception logs `what()` before the process dies; called
  first in `App::init()`. Never logs decrypted plaintext or key material (invariant #5).
- `safe_print.h` — `platform::safe_println<Args...>(stream,fmt,args...)` wraps `std::println`
  in try/catch, swallowing any `std::system_error` from a failed write (Windows Release
  windowless stdout/stderr). Every diagnostic print call site must go through this wrapper
  instead of raw `std::println`.
