# Module: ui/screens — Screen implementations, gallery/favorites/search/tag views

Core UI screens: unlock, gallery browsing, image favorites, tag management, vault selection, advanced search, import status.

## Screens
- `unlock_screen.*` — password + optional keyfile unlock. `secure_text_input.*` /
  `unlock_logic.*` back it (secure entry field + unlock logic). `passphrase.*` helpers
  (`generate_passphrase` fills a `SecureTextInput` directly, never a std::string).
  `unlock_job.*` — `UnlockJob`: runs open+unlock / create (both pay the Argon2id KDF) on a
  `std::jthread` so the screen never freezes; copies password+keyfile into mlock'd
  SecureBytes on start (worker wipes them after the vault call), `active()` /
  `take_outcome()` polling like FileOpJob, no cancel (KDF not interruptible; dtor joins).
  While active the screen swallows ALL input (incl. Esc — a nav would tear the screen down
  under a worker holding &vault_), `animating()` returns true to keep frames ticking, and
  render shows "Deriving key…". Tests: `test_unlock_job.cpp` (real temp vaults, tiny
  KdfParams).
- `gallery_grid.*` — GalleryGrid: Grid + detailed List views (key `L`), live width reflow,
  centred/elided labels. `Shift+S` cycles a gallery's persisted sort_key; breadcrumb shows
  "Sort: <label>" once non-Manual. Ctor takes `initial_view` (default Grid) + a
  `GallerySessionState& session_` member (written DURING the instance's lifetime, since a grid
  descends through sub-galleries without being destroyed): `open_selected()`/`go_up()` call
  `session_.record(nav_.path(),index)` just before `nav_.enter()`/`nav_.up()`, then
  `nav_.select(session_.recall(new path))` just after `refresh()`. Free friend
  `current_gallery_view(const GalleryGrid&)` lets App read view_ on exit. `App::to_gallery`
  seeds a freshly constructed grid via `session_.recall(path)` unless `explicit_index` (App
  sets that only when the outgoing screen was an ImageViewer — the one nav.index that is a
  real freshly-known position); every other ToGallery source passes 0 ("no opinion").
  Phase 48: `content_width(const GalleryGrid&)` free friend returns the SINGLE layout-width
  source (window minus detail panel) — render(), scroll-to-selection, on_enter's cols_ seed,
  and hit_test all route through it; using win_.width() directly desyncs picking from drawing
  whenever the panel is open. Its vertical twin `content_bottom(const GalleryGrid&)` (= window
  height minus the reserved `FOOTER_H` status band, via `grid_bands`) is the SINGLE
  content-bottom source: visible-range culling, the render clip rects, `ensure_visible` /
  `clamp_scroll`, and hit_test all use it, so tiles/rows stop above the footer instead of
  scrolling under its text. Scroll-to-selection is a ONE-SHOT request, not per-frame: private
  `ScrollFollow{None,Ensure,Center}` member `follow_`, applied then cleared by
  `update_scroll_to_selection` (which itself only clamps every call — following every frame
  would fight the mouse wheel, which scrolls without moving the selection → snap-back jitter).
  Arrow keys and the `L` density cycle request `Ensure` (minimal `ui::ensure_visible`);
  on_enter / go_up / open_selected-descend / jump_to_gallery request `Center`
  (`ui::center_scroll` in grid_layout — unclamped centering, then `clamp_scroll` = "as centered
  as the range allows"). on_enter applies the follow immediately (apply_nav runs after this
  frame's update(), so deferring would paint the first frame uncentered for up to an idle
  heartbeat). on_vault_changed deliberately requests nothing — an import drain keeps the
  user's scroll. `render_grid`/`render_list` take `bottom`, NOT the window height.
  hit_test rejects `my` outside `[OY, content_bottom)` — the chrome is not pickable.
  **Phase 50:** Gain `on_vault_changed()` virtual (overridden by GalleryGrid/ImageViewer/FavoritesScreen/AdvancedSearchScreen);
  called by App when the index tree changes (import drain, add/delete, etc.) to refetch stale IndexNode* refs. Password-at-enqueue
  for encrypted archives (wrong password → task Failed, no re-prompt). Footer priority: error > import summary > status.
  Exclusive-op guards: delete/transfer/combine/compact blocked when import queue non-empty ("Imports running — press Shift+I").
  **Phase 51:** `[O]` key opens FolderDialog with Purpose ImportFolder and multi-select; naming popup prefilled with folder basename;
  multiple folders enqueued with auto-naming from their own stems, no prompt. Tile counts cached parallel to children_.
- `favorites_screen.*` update() mirrors GalleryGrid's one-shot scroll-follow: `bool
  follow_scroll_` set by the arrow-key nav cases, applied (ensure_visible) then cleared by
  update(), which otherwise only clamps — per-frame following fights the wheel (snap-back
  jitter). No Center mode: entering always lands on index 0 at scroll 0. NOTE:
  `FavoritesImages::update` (also inherited by TagImages) overrides the base and MUST call
  `FavoritesScreen::update(dt)` first — dropping that call silently loses all scroll
  clamping/following for both image grids (that exact bug shipped for a while).
- `favorites_screen.*` render clips scrolled tiles to below OY (the fixed header) and draws a
  BORDER hairline there — without the clip a scrolled tile paints over the title/[F1]/status.
  Covers all four subclasses. `tag_overview.*` paginates (rows never scroll partially), so it
  needs no clip.
- `favorites_images.*` — flat grid of favorited images across the vault; opens a
  favorites-scoped viewer (ToFavoriteViewer: prev/next iterate the favorites set, Esc returns
  to the grid). `favorites_screen.*` is the shared base (grid/selection/badge) with
  `handle_extra_key`/`extra_hint`/`show_favorite_badge`/`go_back` virtuals used by the tag
  views. **Phase 68:** the base gains a `SelectionModel` (Space toggles, Enter still opens,
  Ctrl+A select-all-or-clear; cleared by reload()) + B/X/M over the selection via an owned
  `CollectionBatchOps ops_` — B routes `batch_favorite_target` → `set_favorites_batch` then
  reload() (unfavorited tiles vanish); X/M go through ops_ (per-parent grouping from
  `SearchHit.path`). Ctor takes a public `CollectionOps` ctx struct (file/folder dialogs,
  ImportQueue&, SecondVaultSession*, active_path — App-owned, passed by const ref, all four
  subclasses forward it). While `batch_ops_busy()` update() only polls and render() draws
  chrome + ops modal (no tile draws → no vault decode submits). Detail panel shows the
  Phase 48 aggregate for 2+ selected (key includes `sel_.revision()`; inherited passed empty —
  collection hits are not siblings). KEY_DOWN handling lives in `handle_key_down` (S3776
  split); an n/N counter is right-aligned on the title line.
- `favorites_galleries.*` — flat grid of favorited galleries; navigates the normal grid
  (Shift+F). `B` toggles favorite on the focused grid tile / current viewer image; gold star
  badge on favorited tiles.
- `vault_manager.*` — multi-vault home screen: lists known vaults from VaultRegistry;
  open/create(save dialog)/remove/lock/select. Emits `NavKind::ToVaultManager`/`LockActive`/
  `ToUnlock(path)`/`ToGallery`. `C` emits `NavKind::ToSettings` (Phase 49; it opened the
  now-deleted ThemePicker before). **Phase 66:** a warm vault row shows an "unlocked · m:ss" or
  "unlocked · session" badge; selecting it promotes the warm handle to active with no password.
  `L` on the warm row emits `NavKind::LockSecond` to lock it immediately.
- `advanced_search_screen.*` — `Shift+/` (`NavKind::ToAdvancedSearch`) dedicated screen:
  keyboard query builder (Tab cycles fields, autocomplete dropdown) + live result list +
  saved-searches sidebar (Ctrl+S save, Enter load/open, Del delete). Image result -> gallery
  viewer; gallery result -> ToGallery. Coexists with the `/` overlay. Ctrl+L toggles the
  result panel List <-> thumbnail Grid; owns its own `DecodeWorker` (update() pumps it);
  `render_result_grid` free friend reuses the shared tile_thumb draw. Query/params/cursor/view
  persist across visits via session-scoped `ui::AdvancedSearchState` App owns + resets on vault
  change; restored on_enter / saved on_exit (results re-derived, node ptrs not persisted).
  Ctrl+R clears the query behind a Y/N modal. Phase 48: detail panel + `Ctrl+D` toggle (bare
  `D` types into query buffer); all result repopulation funnels through `rerun()`, which clears
  the cache key. **Phase 56:** list layout derives from `list_layout.*` module.
- `tag_overview.*` — `Shift+T` first-class Screen (`NavKind::ToTagOverview`): scrollable tag
  list (Up/Down, Enter, Tab=toggle sort, `/`=enter filter mode then type, `` ` ``=quick-switch).
  **Phase 54:** filtering needed an explicit `filtering_` flag — the old browse-mode gate
  `(!filter_.empty() || c=='/') && c!='/'` is false for EVERY input, so type-to-filter never
  worked; bare letters cannot be repurposed because `E` opens the description prompt. Its two
  private helpers `truncate_to_byte_limit`/`tail_clipped_text` were deleted (TextInputModel's
  byte cap and `layout_text_field` supersede them, for every field rather than one). Counts
  from `VaultSearch::tag_overview`; Enter -> TagGalleries. **Phase 51:** two-line rows (chip
  + counts, then dim description or `(no description — [E] to add)`); `[E]` inline edit prompt
  reusing the settings-overlay pattern; description drawn via `ui::fit_text`. **Phase 55:**
  holds a `platform::FileDialog&` (passed by App); `Ctrl+I` (modifier chord — bare letters
  belong to the filter/prompt) opens a `Purpose::TagJson` pick, `update()` drains it,
  `import_tag_dict()` reads + parses + applies + does the ONE `set_vault_settings` commit
  and only then shows the summary modal (`import_summary_`, non-empty exactly while up,
  any key dismisses, owns every key while up). A failed commit sets `error_` and shows no
  modal — a failed write is never reported as a successful import. **Phase 56:** prompt geometry
  derives from `prompt_layout.*` module. **PR #148:** `Ctrl+X` junk-tag cleanup — Y/N confirm
  modal (compact-confirm pattern) -> `Vault::prune_tags(tag_has_renderable_text, &stats)` ->
  summary modal "Removed N junk tags from M items" + `reload()`. The summary modal's title is
  the `summary_title_` member (dict import and cleanup each set their own).
- `tag_galleries.*` — galleries-only view of galleries directly carrying one tag
  (`NavKind::ToTagGalleries`, tag in Nav::path); thin FavoritesScreen subclass over
  `VaultSearch::galleries_with_tag` whose `go_back()` returns to the overview. Tab toggles to
  the images face via the `handle_extra_key` hook.
- `tag_images.*` — images/videos directly carrying one tag (`NavKind::ToTagImages`). Subclass
  of FavoritesImages (reuses off-thread thumb decode + tile draw); `fetch()` ->
  `VaultSearch::images_with_tag`. Tab -> galleries face; Enter opens a collection viewer over
  the tagged set (`NavKind::ToTagViewer`; `App::to_tag_viewer` mirrors to_favorite_viewer);
  `go_back()` -> tag overview. No favorite badge.
- `duplicates_screen.*` (Phase 61) — `NavKind::ToDuplicates`, opened via **`Ctrl+D`** on the
  gallery grid (exclusive op gated on `queue_.busy()` like compact; F1 "Vault tools" group
  documents `Shift+C` + `Ctrl+D`). `State{Choose,Scanning,Review,Done}`: Choose is the mode
  chooser as the screen's initial STATE (exact / exact + visually similar — not a modal
  dialog class). **Phase 86 scan scope:** Choose also carries a scope line
  (Left/Right cycles Whole vault / This gallery only / This gallery vs whole
  vault), shown only when `back_.path` is non-empty — the browsed gallery path
  already arrives in the back-nav Nav from `App::to_duplicates` (grid or active
  dual pane), so no new plumbing. Mode + scope live in the nested
  `ChooseState choose_` struct (`sel` + `scope`; folded into one struct to keep
  the class ≤ 20 fields, S1820). GalleryOnly passes the path to
  `collect_scan_items` (subtree-only walk, proportionally cheaper);
  GalleryVsVault scans the WHOLE vault and `finish_scan` runs
  `scope_filter_groups` before building the review (outside copies stay in
  surviving groups); the review header appends `dup_scope_label` when scoped;
  rescan from Done keeps the scope; both scan modes work in every scope.
  Scanning polls the `DupScanJob` with progress + graceful Esc-cancel; Review
  lists groups largest-reclaimable-first as side-by-side member tiles (thumb/poster, name,
  parent path, size, resolution) with KEEP/REMOVE badges — `Left/Right` member focus,
  `Up/Down` group focus, `Space` toggle (refused with a footer notice when the member is
  its group's last keeper), `A` keep only the focused member, `Enter` full-screen inspect
  of the decoded ORIGINAL, `Esc` prompts only if USER-TOUCHED unapplied marks exist
  (`DupReview::touched()`, also gates `blocks_idle_lock` — Phase 63: groups arrive
  pre-marked keep-first/remove-rest, and untouched defaults are not invested work). The inspect
  texture is OWNED by `InspectState` (uploaded on first overlay draw, destroyed by
  `close_inspect()` on close/replace/dtor; request keys carry bit 63) and deliberately
  NEVER enters the shared thumbnail TextureCache — a full-res upload there evicted the
  review thumbs and could evict the inspect itself mid-view (black-box blanking; same
  rationale as FullTexCache). A failed upload (GPU max texture size) closes the inspect
  with a status message.
  Review-row geometry is the pure `dup_layout.*` module (`dup_row_layout`,
  `dup_tile_height`, `dup_footer_height`, unit-tested): tiles share the full content
  width, centered, scaling down as member count grows; tile height follows the window;
  every vertical extent is font-derived via `ui::line_pitch`; header/footer are opaque
  chrome bands (reserve, never overlay). `dup_layout.cpp` is listed explicitly in
  osv_tests' premake5.lua files{}.
  **Phase 64 — waves + memory fix.** Review presents groups in waves of
  `DUP_WAVE_GROUPS`=20 (current wave = the model's front window); rendering is
  VIEWPORT-CULLED — only rows intersecting the view draw/fetch (uniform `row_h`
  arithmetic) and `worker_.retain(visible thumb keys + inspect key)` prunes
  queued decodes each frame. Pre-64 the screen drew/fetched ALL groups every
  frame and churned the 256 MiB texture LRU (decrypt→decode→evict→refetch,
  observed ~8 GB RSS).
  Apply (per wave): `Ctrl+Enter` → default-cancel confirm (the CURRENT wave's
  count + size) → ONE main-thread `vault::remove_media_batch` → accumulate into
  the nested `WaveTotals` struct (applied files/bytes, waves applied/skipped,
  done_summary) → `finish_wave()` → `ui::refresh_review_members` re-resolves the
  remaining spans from the index (Windows auto-reclaim may compact() and
  RELOCATE chunks) → `failed_.clear()` + shared `cache_.clear()` (both
  offset-keyed; compact can reuse offsets) → `advance_after_wave()` (close
  inspect, reset focus/scroll; empty → Done with the accumulated summary). `N`
  skips the wave (default-cancel confirm when touched marks exist; no vault
  mutation). The three confirm overlays (apply/leave/skip) share the file-local
  `draw_confirm_box` chrome. `start_scan()` resets `WaveTotals` + `stale_` (the
  banner previously never cleared); `on_vault_changed()` untouched — it only
  fires from the import-queue drain and dedupe is exclusive-gated, so an own
  apply never trips the stale banner. Done reports totals across all waves;
  grid refreshes via `on_vault_changed()`. `blocks_idle_lock()` true while
  scanning or current-wave TOUCHED marks exist.
  The all-REMOVE group invariant is enforced in `dup_model` (warning render + apply refused).
- `import_status_screen.*` — `NavKind::ToImportStatus`, opened via **`Shift+I`** or footer click. Shows:
  current item (name, progress bar, source kind), queued items (Ctrl+Up/Down reorder, Del cancel), finished/failed with outcomes.
  `C` clears finished entries. Lane-failure banner surfaces hard stops. Esc returns to previous screen.
  Free friend gate (exclusive-op guards): blocks delete/transfer/combine/compact when queue is non-empty.
  **Phase 56:** Selection is now id-based (not positional index) so `Ctrl+Up`/`Ctrl+Down` keeps focus on the item it just moved.
  Each row is two lines — `source → destination` above, progress bar or outcome below — via `import_status_row.*` module.
  Summary modal geometry via `prompt_layout.*` module.
