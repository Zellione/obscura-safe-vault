# Module: app/ + platform/ — lifecycle & OS integration

Referenced from `mem:core`. Covers `src/app/` (state machine + event loop) and
`src/platform/` (config dirs, dialogs, persistence, hardening).

## app/ — main.cpp, app.{h,cpp}
- State machine + SDL event loop. Event-driven: blocks on `SDL_WaitEventTimeout` when idle,
  renders only on input / async result / animation (`Screen::animating()` +
  `mark_dirty`/`consume_dirty`), so the GPU idles instead of free-running. VSync on
  (`Window::vsync()`); manual ~60fps cap fallback when VSync is unavailable.
- VaultManager is the home screen. App owns ONE unlocked vault (`active_`) at a time + a
  transient `pending_` during unlock + a second app-owned slot `second_` (Phase 66, warm vault).
  Single-active / lock-on-switch: opening another vault wipes the old key; shutdown wipes both
  active and second. `promote_pending()` runs only on unlock success (ToGallery while state==Locked).
  **Phase 66:** App intercepts ToUnlock and checks if the destination matches `second_`: if so,
  calls `second_.take()` to promote the warm handle to active, skipping the unlock password
  entirely (first unlock each session always requires the password, so at-rest security is
  unchanged). After transfer/combine success into the warm vault, the slot's sliding reset is
  called. Wipe paths (lock-now, switch, LockSecond, shutdown) all call `second_.wipe()` to
  zero the mlock'd key. Idle auto-lock deliberately does NOT wipe KeepSession mode (owner
  requirement — no key wipe except explicit user action).
- **Phase 50:** App owns `ui::ImportQueue queue_` (declared after vaults for destruction order, so queue drains before vault wipes key).
  `App::update(dt)` drains queue each frame: `queue_.drain(dt)` attaches staged nodes + triggers `on_vault_changed()` broadcast
  on all active screens (GalleryGrid, ImageViewer, FavoritesScreen, AdvancedSearchScreen refetch cached IndexNode* refs).
  `should_auto_lock` gained `import_busy` parameter (suppresses idle lock while queue non-empty).
  `PendingLockConfirm` modal parks LockActive/ToUnlock/Quit behind a Y/N confirm ("N imports pending — finish current file, discard the rest, and lock?")
  when queue is non-empty; SDL_EVENT_QUIT also flows through this gate. `replay_nav_` mechanism re-enters `apply_nav` after SDL_EVENT_QUIT confirmation.
- **Phase 68:** the four collection-screen ctors (`to_favorite_images/galleries`,
  `to_tag_images/galleries`) take a `ui::FavoritesScreen::CollectionOps{dialog_,
  folder_dialog_, import_ui_.queue, &second_.session, vault_state_.active_path}` ctx, and
  `to_advanced_search` passes the analogous `ui::CollectionBatchOps::Deps` — both wire the
  batch B/X/M multiselect flows (export + grouped transfer) into those screens. The
  FolderDialog stays App-owned (its async callback must outlive any screen).

### Perf instrumentation (Phase 58)
- **`App::update(dt)` extracted from `run()`** — behavior identical, but extracted to host the
  `app.update` scope for `platform::PerfScope` tracing. Render loop is now `frame()` (rendering)
  + `update()` (game logic / UI refresh). See `platform/perf.*`.

### Idle auto-lock
- `app/idle_timer.h` `IdleTimer` (reset on user input in `dispatch_event`);
  `maybe_auto_lock(dt)` wipes `active_` + `to_manager()` after `IDLE_LOCK_SECS = 5 min`.
- `app/auto_lock.h` `should_auto_lock(has_active, blocks_idle_lock, keep_unlocked,
  import_busy, migration_active, timer, dt)` — pure, unit-tested extraction of the 5
  suppression guards (no active vault / a screen's `blocks_idle_lock` / the session-only
  `keep_unlocked_` toggle / a busy import queue / a running `MigrationJob` — Phase 79: the
  job is App-owned, so no screen or queue signal covers it; without this guard a >5-min-idle
  vault upgrade got its vault locked+destroyed under the running coordinator and the progress
  modal wedged forever), all of which reset the IdleTimer instead of ticking it.
- Phase 79 companions in App: `maybe_auto_lock` firing while the upgrade OFFER modal is up
  (job not started — lock is the right default there) also closes the offer + releases import
  exclusivity; the `take_outcome()` poll in `update()` is NOT gated on `vault_state_.active`
  (a gated poll is what wedged the modal); `shutdown()` calls
  `MigrationJob::abort_and_join()` before any vault teardown (window close mid-upgrade).
- `keep_unlocked_` is a plain App bool, flipped by GalleryGrid's `U` via
  `NavKind::ToggleKeepUnlocked` (`App::apply_nav` flips it in place — no screen swap); reset
  to false in `promote_pending()` + the LockActive nav case, so re-unlocking always starts
  with auto-lock ON. `App::render_frame` draws a corner badge ("Auto-lock off [U]",
  `draw_keep_unlocked_badge`) over whatever screen is active whenever `active_ &&
  keep_unlocked_` — an App-level overlay, not per-screen. **Phase 66:** adds a second corner
  badge stacking below keep_unlocked: `draw_second_vault_badge` reads a global `second_vault_status()`
  snapshot and renders "2nd vault unlocked" + live countdown/session label whenever the slot
  occupies a key. Badge does NOT fade (unlike keep_unlocked's 10 s timeout). A dirty flag
  `second_badge_secs_` marks status changes; update() ticks `second_` and invalidates on mode
  change or seconds-tick. The slot is owned by App; visibility is tied to `second_.occupied()`.

### Event handling (Phase 56)
- `back_click.{h,cpp}` — `is_back_click(SDL_Event&)` detects right button-down; `make_back_key_event()` constructs a synthetic Escape key-down. `App::dispatch_event` translates every right button-down into an Escape and swallows the release, so the button mirrors Esc exactly everywhere (grid multi-selection clears first, fullscreen exits on first click, modals all inherit Esc handling). Pure helpers are testable without a window.
- `App::pump_events` and `gfx::Window` — HiDPI mouse coordinate fixing. `pump_events` runs `SDL_ConvertEventToRenderCoordinates(renderer, &e)` on each SDL event before dispatching, converting button/motion/wheel positions from window points into render-pixel space. `Window::mouse_x()`/`mouse_y()` route `SDL_GetMouseState` through `SDL_RenderCoordinatesFromWindow`. Both conversions are identity at 1.0 density (Linux dev box) and scale at >1.0 (Windows). Hit-testing (tile clicks, strip hover, video seek bar) and edge-click navigation now land where the cursor is on all displays.

### Session state
- App groups all session-scoped UI state into one `SessionUi sessions_` member (cpp:S1820
  field-cap grouping, same pattern as VaultState/Overlays/MigrationUi/ImportUi): `.adv`
  (`AdvancedSearchState`), `.dual` (`DualSessionState`, Phase 78 split view), and `.gallery`
  (`GallerySessionState`). All three reset at the same points: LockActive, idle auto-lock,
  and promote_pending (vault switch).
- `sessions_.gallery`: last GalleryGrid view density (List/GridS/GridM/GridL/GridXL) +
  ImageViewer strip side + a single "last video watched" resume bookmark, carried across
  App's screen reconstruction on every nav transition. `capture_session_state()`
  (dynamic_cast onto the active Screen) snapshots it right before `on_exit()`;
  `to_gallery`/`to_viewer`/`to_favorite_viewer`/`to_tag_viewer` feed
  `sessions_.gallery.view`/`strip_side` back in as the new screen's initial ctor arg.
  `enter_viewer()` is the shared tail of every ImageViewer construction: `on_enter()` then
  `ui::apply_video_resume()`.
- App also owns `HelpPopupState` (intercepts F1 globally, renders the overlay on top).
- **Overlay dispatch structure (Phase 65 cleanup).** `dispatch_event` delegates to the static
  `App::dispatch_overlay_event`, which is now only a fan-out: it calls, in priority order,
  `OverlayDispatch::help` → `::settings` → `::migration` → `::lock_confirm`, returning on the
  first that reports the event handled. `App::OverlayDispatch` is a nested struct declared in
  `app.h` and defined in `app.cpp`; nested rather than more App members because a nested class
  reaches the enclosing class's privates exactly as a member does, without growing App's own
  interface (App sits just under Sonar's `cpp:S1448` 35-method cap — five more members trips it).
  The ordering described below is unchanged, only relocated.
  One non-obvious contract: on a non-empty scan the settings VaultOps migration trigger closes
  settings and deliberately reports the event as NOT handled, so the offer modal receives the
  same event and renders on this frame rather than the next. Do not "tidy" that into a `return
  true`.
- Phase 49: App owns `ui::SettingsState settings_` and intercepts **F2** globally, mirroring
  the F1 convention — the overlay draws over whichever screen is active, so `Esc` returns to a
  paused video / scroll position intact (it is deliberately NOT a `Screen`). Event order in
  `dispatch_event` matters: the F1 toggle first, then the `help_.open` guard, THEN F2 and the
  `settings_.open` guard — so F1 still opens help over an open settings panel, and while both
  are open the help popup (which draws on top) keeps its arrow/wheel events instead of losing
  them to the panel behind it. `App::open_settings_overlay()` is the SINGLE seeding point for
  `vault_unlocked`/`draft`/`theme`, called from both the F2 handler and the `ToSettings` nav
  case — two copies would drift and a stale `vault_unlocked` is exactly the bug that survives
  review. `active_` is a nullable `unique_ptr<Vault>`, so every settings path guards it.
  **Phase 84:** `open_settings_overlay` also seeds `settings_.gallery_view` from
  `sessions_.gallery.view`; the Appearance section has TWO rows (theme, Default Gallery View —
  `settings_change_value` routes by `state.row` through `change_appearance_value`). After the
  overlay handles an event, `OverlayDispatch::settings` syncs `settings_.gallery_view` back
  into the session AND pushes it into a live grid via the `ui::set_gallery_view` free friend
  (dynamic_cast; sets `view_` + ScrollFollow::Ensure) — placed inside the handled branch, so
  the VaultOps migration-trigger "deliberately not handled" contract is untouched. The
  overlay's LEFT/RIGHT value branches share ONE `apply_value_delta(state, delta, commit_out)`
  helper (Sonar S3776/S134; row 0 theme + ThemePref save, row 1 GalleryViewPref save, Security
  SecondVaultPref save — all commit_out=false; vault-backed sections commit_out=true).
- `NavKind::ToSettings` (Phase 49, emitted by VaultManager's `C`) is one of the few kinds
  EXCLUDED from `apply_nav`'s screen teardown (alongside `ToggleKeepUnlocked`/`Quit`/`None`) —
  the overlay draws over the screen, so tearing it down would rebuild the vault manager
  underneath. The teardown lives in a guard clause ABOVE the switch, not in any case.
- `ui::draw_help_popup` synthesises a global "Global" group (F1/F2) so both appear on every
  screen: `Screen::help_groups()` is a per-screen virtual with eight overrides and had no
  shared entry point. `help_line_count` and the scroll clamp must be fed the SAME list that is
  drawn, or the popup's scroll bound silently breaks.

## platform/
- `path_utf8.h` (Phase 70) — header-only UTF-8↔path vocabulary: `utf8_to_path`,
  `path_to_utf8` (no-throw), `path_to_utf8_generic`, `fopen_path`/`freopen_path`
  (`_wfopen`/`_wfreopen` on Windows). Pure std — the ONE platform/ header includable
  from any module (vault, gfx, ui, app). Exists because narrow `path::string()`/
  `path{std::string}` go through the ANSI code page on Windows (throwing on CJK —
  the Phase-70 import crash) and are now banned in src/ (see `mem:conventions`).
- `paths.*`, `file_dialog.*` — config dirs, SDL file dialogs (`save_vault()`). Each open is
  tagged with a `Purpose` + `take_result(Purpose)` so one shared dialog polled by two handlers
  (image pick vs zip import) can't steal each other's result. `Purpose::TagList` +
  `open_tag_list()` (.txt); `open_zip()`'s filter accepts `zip;cbz`. Externally-supplied paths
  (dialog results, `vaults.list` lines) go through `platform::normalize_user_path` before they
  reach `fopen`. **Phase 72:** the dialog callbacks store their picked paths via
  `platform::normalize_external_path_utf8` (paths.h: `normalize_user_path` → `path_to_utf8`)
  — the ONE sanctioned dialog→`std::string` conversion. They previously used
  `norm->string()`, which on Windows throws for CJK names inside the SDL callback (the
  Phase-72 import crash). Consumers convert the stored UTF-8 strings back with
  `utf8_to_path`, never the narrow `fs::path` ctor. `config_dir()` decodes SDL_GetPrefPath's
  UTF-8 with `utf8_to_path` too.
- `folder_dialog.*` — export destination picker (same Phase-72 UTF-8 storage rule).
- `locale_init.h` (Phase 72) — header-only `platform::init_locale()`: switches **LC_CTYPE
  only** (never LC_NUMERIC — decimal-comma corruption) to a UTF-8 locale; env locale with
  `C.UTF-8` fallback on POSIX. **Deliberate NO-OP on Windows** — libarchive keeps the wide
  name there regardless (ArchiveReader's wide fallback covers 7z/RAR), and any non-"C" CRT
  locale makes libarchive (get_current_codepage reads setlocale) build an OEM(CP437)→locale
  conversion for tar names, mangling raw UTF-8 bytes into valid-but-wrong UTF-8. Called first
  in `app/main.cpp` and `tests/test_main.cpp`, before any threads. Exists because libarchive
  converts 7z/RAR entry names (UTF-16 in-header) through the current locale at parse time and
  returns NULL names under the default `"C"` locale (see ArchiveReader in `mem:module/ui`).
- `vault_registry.*` — recent-vaults list: config-dir file of known vault PATHS ONLY (no
  secrets); `list`/`add`(move-to-front,dedup)/`remove`/`seed_if_empty`; atomic temp+rename.
- `theme_pref.*` — chosen UI theme persistence: `config_dir()/theme.conf` holds the theme's
  stable slug ONLY (no secrets); `load()`->ThemeId (missing/unknown -> default), `save(id)`;
  atomic temp+rename (mirrors vault_registry). Loaded in `App::init()`, saved live by the
  `F2` settings overlay's Appearance section (Phase 49; ThemePicker, which used to do this,
  was deleted).
- `gallery_view_pref.*` (Phase 84) — persisted gallery view density: `config_dir()/
  gallery_view.conf` holds the `ui::gallery_view_slug` token ONLY (no secrets);
  `load()`->GalleryView (missing/unknown -> GridM), `save(view)`; atomic temp+rename (exact
  ThemePref mirror, incl. platform/ including a ui/ header the way theme_pref includes
  gfx/theme.h). Loaded in `App::init()` + re-seeded after promote_pending's session reset;
  saved live by the grid's `L` key and the F2 Appearance row.
- `second_vault_pref.*` (Phase 66) — per-machine default for cross-vault keep-open mode:
  `config_dir()/second_vault.conf` holds the mode (LockNow/KeepTimed/KeepSession) ONLY (no
  secrets); `load()`->SecondVaultMode (missing/unknown -> LockNow), `save(mode)`; atomic
  temp+rename. Loaded in `App::init()`, saved live by the `F2` settings overlay's Security
  section. Nothing persists per-vault or per-session state; only the per-machine default is
  stored.
- `VolumePref` — `config_dir()/volume.conf`, one float [0,1], atomic write, missing/invalid
  -> 1.0; App loads at init + saves on clean exit (the in-memory global lives in
  `media/volume_setting.*`, not AV-gated).
- `harden.{h,cpp}` — `disable_core_dumps()`: `prctl(PR_SET_DUMPABLE,0)` +
  `setrlimit(RLIMIT_CORE,{0,0})` on Linux, no-op on Windows (macOS removed — `#error` guard in
  `src/crypto/random.cpp`); called once at app init, Release (NDEBUG) builds only, before any
  vault unlock, to keep decrypted data/keys out of core dumps.
  `grow_secure_mem_budget(bytes)`: best-effort growth of the page-lockable budget, called
  once in `App::init()` (ALL configs, 256 MiB) before any SecureBuffer/SecureBytes exists —
  Windows raises the minimum working-set size via `SetProcessWorkingSetSize` (VirtualLock's
  cap; ~200 KB default meant every pixel-buffer lock silently failed), Linux raises soft
  `RLIMIT_MEMLOCK` to the hard limit. Returns whether the platform now reports >= `bytes`
  lockable. Windows caveat: VirtualLock does not protect against hibernation
  (`hiberfil.sys`). The mlock-failure warning text comes from `crypto::mlock_fail_hint()`
  (platform-appropriate advice, secure_mem.h). Also
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
