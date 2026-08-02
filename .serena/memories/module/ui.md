# Module: ui/ — screens, viewer, dialogs, pure models

Referenced from `mem:core`. Covers `src/ui/`: every Screen, the image/video viewer, all
modal dialogs, and the pure SDL-free view/search/sort/session models. Many free-friend
helpers exist purely to keep host Screens under the cpp:S1448 35-method cap.

## Text input (Phase 54) — every field in the app routes through these
- `text_input_model.*` — the shared editing model. `ITextInput` (storage-agnostic
  interface) → `TextInputBase` (ALL caret/selection/word/UTF-8 logic, expressed over
  two storage primitives `bytes()` + `splice()`) → `TextInputModel` (std::string).
  The base is the point: the ordinary and secure backends cannot drift, because
  neither implements any navigation of its own. Also the pure UTF-8/word helpers
  (`utf8_prev_boundary`/`utf8_next_boundary`/`utf8_char_count`/`word_boundary_*`)
  and input sanitising (`acceptable_input_run` — valid UTF-8, no C0 controls or
  DEL, since fields are single-line and a pasted newline must not enter).
  `revision()` is a monotonic counter bumped on CONTENT changes only; hosts whose
  field drives a live filter/autosuggest compare it across the shared handler.
  A size comparison would miss replacing a selection with same-length text.
  `insert()` splices the caller's buffer run-by-run and never allocates an
  intermediate — that is what lets the secure backend honour "no std::string".
- `secure_text_input.*` — `SecureTextInput`, the same interface over a fixed-capacity
  mlock'd `crypto::SecureBytes`. Every splice `crypto_wipe`s the bytes the shift
  vacates; `clear()` wipes the whole buffer. `selection_text()` ALWAYS returns
  empty — copy/cut out of a password field is refused by design (invariant #2 +
  the Phase 45 threat model), and this is the second line of defence behind the
  event handler. `text_view()` aliases the locked bytes for the KDF/strength
  call sites; never copy it into a std::string. **Replaced `SecureTextField`,
  which is deleted.**
- `text_field_view.*` — pure layout: `layout_text_field(text, caret, sel, field_w,
  prev_scroll, measure)` → visible run + caret x + selection rect + scroll,
  following the caret at both edges. Partial glyphs are excluded at both ends so
  the run draws without a clip rect. O(n) via a one-pass prefix-width table (the
  atlas has no kerning, so per-character advances sum exactly). `caret_is_on(now_ms,
  last_edit_ms)` is the blink — a pure function of a monotonic clock, NOT a
  per-frame dt, so no dialog has to grow an `update()` just to blink a caret.
- `clipboard.*` — `ClipboardBackend` seam (SDL-backed by default,
  `set_clipboard_backend()` injects a mock) + `paste_from_clipboard` /
  `copy_selection_to_clipboard` / `cut_selection_to_clipboard`. Paste views the
  backend's buffer directly into mlock'd storage; the SDL backend `crypto_wipe`s
  that buffer in `release_text()` before `SDL_free`. Copy/cut return false for a
  secure field. Orthogonal to `clipboard_secret.h` (Phase 45 auto-clear timer).
- `text_input_event.*` — `handle_text_input_event(ITextInput&, SDL_Event&)`, the ONE
  handler. **Key precedence: a focused field consumes Ctrl+A/C/X/V before its host
  screen** (else Phase 53's gallery Ctrl+A fires while the user selects typed text).
  Does NOT consume Enter/Esc/Tab/Up/Down or Ctrl+Up/Ctrl+Down (detail-panel scroll).
  `field_owns_event()` is the routing predicate for hosts whose EMPTY buffer has its
  own key meanings (advanced-search builder chips, tag editor, tag-overview filter):
  with text present the field owns every editing key, with it empty only typing and
  pasting are the field's. `text_editing_help_group()` is the shared F1 entry the
  four field-owning screens append.
- Draw side lives in `widgets.*`: `TextFieldChrome` (per-field scroll + blink state,
  one extra member per migrated field), `draw_edit_field` (boxed) and
  `draw_inline_edit_text` (bare run, for the inline-laid-out fields). Both paint
  through one private helper so they cannot disagree about caret placement. A
  masked field lays out one `*` per CHARACTER, not per byte. The helper detects an
  edit by comparing against what it drew last frame, so hosts never report edits.
- Tests: `tests/ui/text_input_conformance.h` holds the storage-agnostic suite and
  BOTH `test_text_input_model.cpp` and `test_secure_text_input.cpp` run it — a
  per-backend copy is how the two would diverge. Plus `test_text_field_view.cpp`,
  `test_clipboard.cpp` (mock backend), `test_text_input_precedence.cpp`.

## Screens
- `unlock_screen.*` — password + optional keyfile unlock. `secure_text_input.*` /
  `unlock_logic.*` back it (secure entry field + unlock logic). `passphrase.*` helpers
  (`generate_passphrase` fills a `SecureTextInput` directly, never a std::string).
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
  scrolling under its text. `render_grid`/`render_list` take `bottom`, NOT the window height.
  hit_test rejects `my` outside `[OY, content_bottom)` — the chrome is not pickable.
  **Phase 50:** Gain `on_vault_changed()` virtual (overridden by GalleryGrid/ImageViewer/FavoritesScreen/AdvancedSearchScreen);
  called by App when the index tree changes (import drain, add/delete, etc.) to refetch stale IndexNode* refs. Password-at-enqueue
  for encrypted archives (wrong password → task Failed, no re-prompt). Footer priority: error > import summary > status.
  Exclusive-op guards: delete/transfer/combine/compact blocked when import queue non-empty ("Imports running — press Shift+I").
  **Phase 51:** `[O]` key opens FolderDialog with Purpose ImportFolder and multi-select; naming popup prefilled with folder basename;
  multiple folders enqueued with auto-naming from their own stems, no prompt. Tile counts cached parallel to children_.
- `favorites_screen.*` render clips scrolled tiles to below OY (the fixed header) and draws a
  BORDER hairline there — without the clip a scrolled tile paints over the title/[F1]/status.
  Covers all four subclasses. `tag_overview.*` paginates (rows never scroll partially), so it
  needs no clip.
- `favorites_images.*` — flat grid of favorited images across the vault; opens a
  favorites-scoped viewer (ToFavoriteViewer: prev/next iterate the favorites set, Esc returns
  to the grid). `favorites_screen.*` is the shared base (grid/selection/badge) with
  `handle_extra_key`/`extra_hint`/`show_favorite_badge`/`go_back` virtuals used by the tag
  views.
- `favorites_galleries.*` — flat grid of favorited galleries; navigates the normal grid
  (Shift+F). `B` toggles favorite on the focused grid tile / current viewer image; gold star
  badge on favorited tiles.
- `vault_manager.*` — multi-vault home screen: lists known vaults from VaultRegistry;
  open/create(save dialog)/remove/lock/select. Emits `NavKind::ToVaultManager`/`LockActive`/
  `ToUnlock(path)`/`ToGallery`. `C` emits `NavKind::ToSettings` (Phase 49; it opened the
  now-deleted ThemePicker before).
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
  derives from `prompt_layout.*` module.
- `tag_galleries.*` — galleries-only view of galleries directly carrying one tag
  (`NavKind::ToTagGalleries`, tag in Nav::path); thin FavoritesScreen subclass over
  `VaultSearch::galleries_with_tag` whose `go_back()` returns to the overview. Tab toggles to
  the images face via the `handle_extra_key` hook.
- `tag_images.*` — images/videos directly carrying one tag (`NavKind::ToTagImages`). Subclass
  of FavoritesImages (reuses off-thread thumb decode + tile draw); `fetch()` ->
  `VaultSearch::images_with_tag`. Tab -> galleries face; Enter opens a collection viewer over
  the tagged set (`NavKind::ToTagViewer`; `App::to_tag_viewer` mirrors to_favorite_viewer);
  `go_back()` -> tag overview. No favorite badge.

## Image / video viewer
- `image_viewer.*`, `widgets.*` — viewer has Fit + FillScroll + Slideshow modes, bottom/left
  strip toggle (keys F/T, P starts slideshow). `widgets` has button_state + elide_middle /
  elide_tail (pure, templated on the measure callable) + their font-bound bindings fit_text /
  fit_text_tail — middle by default, tail where the string's start carries the meaning
  (`[key]  description` help lines). Hosts a fit-only VideoPlayback when the current
  item `is_video()`: Space play/pause, J/L ±5s, `,`/`.` frame-step, drag seek bar; M mute,
  volume ∓5% (seek bar seeks video+audio in-sync). Volume via `ui::volume_dir` — `-`/`+` glyph
  keys (HUD `[-/+] Vol`) + `[`/`]` produced char resolved through `SDL_GetModState` (German
  AltGr) + physical bracket scancodes; level seeded from `media::saved_volume()` on open,
  written back on change. `R` toggles loop (process-lifetime `media::saved_loop_enabled()`;
  VideoPlayback re-seeks to 0 and keeps playing at EOF when set); on-screen ring indicator next
  to play/pause. **Phase 56:** `on_vault_changed()` uses the `album_rebind.*` module to re-bind
  by path, preserving zoom, pan, fill-scroll offset, video position and animation frame.
  Chrome: `viewer_chrome(const ImageViewer&)` free friend returns the `ChromeBands` for the
  whole viewer area (window minus strip) — an OPAQUE STRIP_BG header band (name/index/zoom +
  [F1] Help) and an opaque footer band, with the media fit into `.content` only, so a band
  never covers picture/video. `viewport_rect()` IS `viewer_chrome(*this).content`, so zoom,
  pan, the fill-scroll model, mouse hit-testing and drawing all agree. Windowed, both bands are
  always reserved (the image never resizes when a status comes and goes); fullscreen drops the
  header entirely — HUD text included — for an edge-to-edge picture, and forces the footer band
  in only while `viewer_footer_text()` is non-empty. That one free friend is the single source
  for both the reserved height and the drawn line, so they cannot disagree; it prefers the
  export status over the "Video playback unavailable in this build." notice. VideoPlayback's
  own CONTROL_H bar sits inside `.content`, directly above the footer band (same STRIP_BG, so
  the two read as one bar). Ctor gains `initial_strip_side` (default Bottom); three free
  friends —
  `current_strip_side`, `capture_video_resume` (snapshot outgoing viewer's video path+position
  into a GallerySessionState, or clear when the current item isn't a live video),
  `apply_video_resume` (seek a freshly (re)opened matching video to the remembered position,
  called right after `on_enter()` builds video_). "Collection mode" (explicit image set +
  per-image path + exit Nav) lets the viewer serve favorites/tag sets, not just one gallery.
  **Strip fetch windowing (post-Phase-58 fix):** `render_strip` requests thumbnail textures
  ONLY for cells in `strip_visible_range(scroll, extent, thumb, gap, count,
  STRIP_PREFETCH_CELLS)` (pure helper in `strip_layout.*`, ±8-cell margin) and calls
  `DecodeWorker::retain()` with the window's keys each frame so out-of-window queued fetches
  are dropped. Requesting the whole album (the Phase 58 shape) enqueued every thumbnail in
  the album as a background fetch+decode job — on a big cold vault that ground the disk for
  minutes, starved the render thread's video demux reads (~3 fps playback), and an album
  bigger than the 256 MB texture budget re-evicted and re-fetched forever. The badge/hover
  loop is bounded to the same window. Off-window cells draw as placeholders.
- `anim_playback.*` (Phase 47 GIF, Phase 57 WebP; was `gif_playback.*`) — `AnimPlayback`:
  pImpl, decoder libs confined to `.cpp`. Picks its `media::AnimDecoder` backend from
  `node.meta.format` — WebP via libwebp (ALWAYS available), GIF via FFmpeg (`OSV_VENDORED_AV`
  only); no backend -> `valid()==false` and the host shows the static first frame. Auto-loop
  (a file's declared loop count is deliberately IGNORED, so both formats behave alike), Space
  toggles pause, zoom/pan unchanged. Decrypted bytes held in mlock'd `crypto::SecureBytes`
  outliving the decoder. Frames uploaded row-by-row honoring `SDL_LockTexture` pitch.
  **Phase 57 fixed a latent break here:** the file used to open `namespace ui {` INSIDE its
  `#ifdef OSV_VENDORED_AV`, so the `#else` stub landed outside the namespace and the TU did not
  compile at all without vendored FFmpeg. `Impl` is now always compiled; only the GIF backend is
  gated. No CI job builds without `OSV_VENDORED_AV`, so nothing would have caught it.
- `anim_model.*` (Phase 47; was `gif_model.*`) — pure logic: `AnimHoverGate` (200 ms dwell, one
  start-edge per hover), `anim_within_hover_dimension_budget(w,h)`,
  `anim_hover_frame_count_exceeded(frames)`, `anim_frames_to_advance(...)` with 64-frame
  catch-up cap.
- `video_playback.*` — in-viewer player: `VideoDecoder` (demux only, render-thread-side) +
  `VideoDecodeWorker` (codec-level decode, bg thread, see `mem:module/media`) + YUV texture +
  `SDL_AudioStream` (master audio clock) + seek bar (both tracks); mute/volume via
  `SDL_SetAudioStreamGain`; A/V sync via `av_sync::decide`; pause pauses both; pImpl gated on
  `OSV_VENDORED_AV` (non-AV build -> poster). `seek(seconds)` is clamped, does NOT touch
  play/pause (restores a resume bookmark right after ctor; playback opens paused). Impl demuxes
  (`demux_next_video_packet()`) + submits packets by `generation_`, reads back Results. Shared
  helpers `feed_one_packet()`/`prefetch_upto()`/`consume_result()`: `decode_into_pending()`
  blocks (bounded by `wait_result()`'s ~20ms timeout, retried) — used by the ctor's first frame
  + `do_seek()`; `try_advance_pending()` is the steady path, a single `wait_result()` (no
  retry) so `render()` never blocks >~20ms under a slow codec. Both keep the worker's
  `outstanding()` backlog to `PREFETCH_DEPTH` packets ahead and, on a miss, feed one more up to
  `MAX_STEADY_IN_FLIGHT` (uncapped while `skip_pending_` is set, since a seek's decode-forward
  gap is one-time GOP-bounded). `do_seek()` bumps `generation_` + calls `begin_seek()`;
  stale-generation Results are discarded (the worker un-counts every finished job incl.
  silently-discarded seek frames, so no phantom backlog wedges feed). Impl's audio + pending-
  frame state each live in nested `AudioState`/`FrameState` structs (SonarQube struct-size).
- `playback_model.*` — pure video transport maths: clock/clamp/seek-bar map/mm:ss/frame-due
  (pure/tested).
- `slideshow_view.*` — full-screen slideshow SDL plumbing (owns SlideshowModel + cross-fade
  render). `slideshow_model.*` — auto-advance/wrap/shuffle/cross-fade maths (pure/tested,
  driven via update(dt)).
- `full_tex_cache.*` — shared decode→GPU full-res texture cache (decrypt into mlock'd
  SecureBytes, wipe after upload); used by viewer + slideshow.

## Dialogs
- `transfer_dialog.*` — `M` modal: move OR copy selected images / a focused gallery subtree to
  another vault or within the active vault. Source enum `{Images,Gallery,Galleries}`;
  open()/open_gallery()/open_galleries(). Stages: Mode(Move/Copy) → PickingDest (delegated to
  VaultUnlockPicker) → PickGallery (GalleryPickerModel, scrollable + `/`-filterable, "+ New
  gallery…" pinned via set_pinned_suffix) → run `vault::transfer_*` per mode → dest re-locked
  on every exit (src never locked here). Grid skips its import dlg poll while active(); M with
  no selection acts on the focused tile.
- `combine_dialog.*` — `Shift+M` modal: merges the CURRENTLY BROWSED gallery into another via
  `vault::combine_galleries` (same- or cross-vault). Stages PickingDest (VaultUnlockPicker) ->
  PickTarget (GalleryPickerModel over `combine_target_galleries`) -> Running (progress modal).
  `CombineOutcome{status,source_gone,same_vault,dest_path}` drained by GalleryGrid::update()
  for post-combine nav: source_gone && same_vault -> jump_to_gallery(dest_path); source_gone &&
  !same_vault -> go_up(); !source_gone -> refresh() (partial merge from a collision).
  source_gone read as `src_.list(src_gallery_).empty()`.
- `vault_unlock_picker.*` — "pick a destination vault, then unlock it" flow, extracted so both
  TransferDialog + CombineDialog reuse it. Stages PickVault ("This vault" row 0, or a registry
  entry) -> Unlock (password + optional keyfile, skipped for "This vault"). Esc cancels the
  whole flow. Owns a transient dest `vault::Vault`; `close()` is idempotent (locks/wipes only
  if actually unlocked). `is_self()`/`unlocked_vault()` combine with the caller's active vault
  to resolve "the vault to write into".
- `gallery_picker.*` — `GalleryPickerModel`: pure SDL-free filterable/scrollable list model
  shared by TransferDialog + CombineDialog. set_items, open/close_filter (`/`), filter_*,
  move(delta), filtered(), selected(), geom(visible_rows). `set_pinned_suffix(item)` keeps one
  extra row appended after filtering, exempt from the filter. Phase 54: the filter is a
  `TextInputModel` exposed as `filter_input()`; hosts route SDL events into it and call
  `refilter()` when its `revision()` moved. ONE model, TWO drivers
  (`transfer_dialog.cpp` + `combine_dialog.cpp`) — both routing blocks must stay in
  step, and both entry points (`M` and `Shift+M`) need re-testing on any change.
- `folder_dialog.*` (Phase 51) — `FolderDialog`: native file-picker for folders. `Purpose` enum:
  `{None, Export, ImportFolder}`. `open(purpose, allow_many)` → SDL_ShowOpenFileDialog with
  SDL_DIALOG_FOLDER. `take_result(purpose)` returns `vector<std::string>` — phase-scoped
  filtering so export and folder-import results don't cross. Single-select result is
  `result[0]`, multi-select is the whole vector. Mirrored from FileDialog architecture.
  Backed by `FolderDialogTestPeer` for headless tests. Both export call sites guard
  `!result.empty()` before indexing.
- `rename_dialog.*` — `R` modal, renames the focused image/video/gallery via
  `vault::rename_node`. open(gallery_path,old_name)/close()/handle_event/render/
  consume_completed(status_out).
- `tag_editor.*` — `G` add/remove-tags modal in GalleryGrid + ImageViewer. Current-tags list
  scrolls (Up/Down) via pure `tag_scroll.h` + auto-scrolls to a just-added tag. Read-only
  "Inherited from gallery" section (`ui::inherited_tags`, `tag_inherit.*`) below own-tags;
  Del/selection never touch it. **Phase 51:** Read-only "From contents" section below inherited
  (galleries only, non-empty only) — computed by `ui::contents_tags`, threaded by the tag
  editor's `from_contents_` vector. Del/selection never touch it, move_cursor bounds selected_
  to tally_ only (structurally impossible to delete a tag not in the node's own tags).
  Autosuggest dropdown while typing (`VaultSearch::all_tags` vocab refreshed on open/add/remove;
  `ui::editor_tag_suggestions`; Up/Down highlight via move_tag_cursor, -1=buffer; Enter adds the
  TYPED text unless a suggestion is highlighted; Esc deselects first). **Phase 56:** list layout
  derives from `list_layout.*` module.
- `search_overlay.*` — `/` live-filtered search modal in GalleryGrid; Tab cycles scope
  (Both/Images/Galleries). **Phase 56:** list layout derives from `list_layout.*` module.
- `consent_dialog.*` — modal "export anyway?" confirm (SDL plumbing).
- `theme_picker.*` — **DELETED in Phase 49.** Its behaviour moved into the `F2` settings
  overlay's Appearance section (`settings_overlay.*`); the vault manager's `C` now emits
  `NavKind::ToSettings`, which App translates into `open_settings(..., Appearance)`.
- `quick_switch.*` — global `` ` `` (grave) overlay: lists registry vaults; choosing one emits
  `NavKind::ToUnlock(path)` (App locks current + unlocks chosen); Esc or the active vault =
  no-op. Hosted by GalleryGrid, ImageViewer, FavoritesScreen base (take a VaultRegistry& +
  active vault path). `consume_choice()` drains the pick.
- `progress_modal.*` — `draw_op_progress`: shared veil + "N/M" bar + cancel-hint modal reused
  by every screen hosting a background job.
- `help_popup.*` — shared `F1` help popup: HelpGroup/HelpEntry types + pure open/close/scroll
  logic + `draw_help_popup`. `Screen::help_groups()` virtual (default empty) supplies
  per-screen grouped content; App owns HelpPopupState + intercepts F1 globally. Esc/Q close.
  **Phase 51:** `HelpPopupState::scroll_line` is an int line index (not pixels); `clamp_help_scroll`
  retired, replaced by `clamp_help_line`. Clip band sized to `visible_lines * LINE_H`; clip rect
  via `lround`, not truncation. `help_layout.*` new module: `help_visible_lines(popup_h, lines_per_column)`
  returns the number of lines that fit in a viewport; `HelpColumn` / `help_column_count(groups, lines_per_column)` /
  `pack_help_columns(groups, lines_per_column)` handle two-column packing above a width threshold,
  never splitting a group across a column boundary. Single-column fallback below the threshold.
  Scroll affordance from theme TEXT_FAINT when content overflows. Tests verify first/last line fully
  inside the band, every line reachable, group boundaries never split, column budgets never exceeded.

## Export (the one gated invariant-#1 deviation)
- `selection_model.*` — multi-select state for export (pure/tested). Phase 53 adds
  `select_all(count)` / `all_selected(count)` behind Ctrl+A; `select_all(0)` does NOT clear
  ("select all of nothing" is not a deselect) and `all_selected(0)` is false (else Ctrl+A on an
  empty gallery clears forever). Phase 48: gained
  `revision()`, a monotonic counter incremented on `toggle()` and `clear()`, used as a cache
  key by the detail panel.
- `export_ui.*` — shared consent + folder-pick plumbing used by gallery + viewer.
- `export.*` — decrypt→write-verbatim→wipe export (SDL-free/tested). The ONE deliberate
  deviation from invariant #1: writes decrypted originals to disk on explicit user consent
  (selection-only, never thumbnails, buffer wiped right after write).

## Background jobs (mirror each other; each owns the vault's single-thread file handle)
- `zip_import_job.*` — ZipImportJob runs import_cbz/import_zip (start_cbz/start_zip, shared
  launch()) on a bg `std::thread`; start_archive/start_archive_cbz wrap the same launch() for
  the libarchive path. Contract: while active() the worker owns the vault handle, so GalleryGrid
  must NOT touch the vault (update()/render()/handle_event() short-circuit — no thumbnail
  reads/listing); it polls total()/done() + take_outcome() (joins) + draws a progress modal,
  Esc -> cancel(). poll_import_job keeps the naming state active across a password round-trip
  (encrypted zip/cbz) and clears it on a terminal outcome.
  `Screen::blocks_idle_lock()` (default false; GalleryGrid returns import_job_.active()) stops
  App::maybe_auto_lock wiping the key mid-write. Unit-tested (poll-to-completion harness).
- `file_op_job.*` — FileOpJob runs export / delete / move-copy on a bg worker (same contract).
  start_export/start_delete/start_transfer_images/start_transfer_gallery/
  start_transfer_galleries/start_combine -> `FileOpOutcome{ok,cancelled,done,failed,status,...}`.
  GalleryGrid owns one for export+delete; TransferDialog owns one (Running stage). ImageViewer's
  single-image export stays synchronous. The GalleryGrid gate
  (vault_busy/poll_file_job/handle_job_input) is free friends. Unit-tested.

## Import planning & archive reading
- `folder_scan.*` (Phase 51) — `scan_folder(root) -> vector<ZipEntry>` via
  `std::filesystem::recursive_directory_iterator` with `skip_permission_denied` and symlinks
  skipped (never followed, containment verified). Emits archive-style relative paths as `ZipEntry`,
  bounded by an entry-count limit. Error recovery: permission denied non-fatal, continue scan.
- **Phase 53 recursive + multipart archive stack** (each pure/SDL-free unless noted):
  - `archive_kind.*` — `detect_archive_kind(name,bytes)` (extension proposes, magic confirms) and
    `is_archive_name(name)` (extension only). The split is deliberate: the planner has entry NAMES
    but no bytes, so it classifies permissively and the orchestrator magic-checks once bytes exist,
    demoting a liar to `skipped_unsupported`. `archive_extension_of` is the shared table so the two
    cannot drift.
  - `recursive_import.*` — `walk_archive`, depth-first, backends INJECTED as hooks (so this module
    links neither miniz nor libarchive and the recursion is testable with fakes). A recursive plan
    cannot be a flat placement list — each placement indexes a *different* archive buffer — so it
    emits through hooks as it goes. Depth-first is load-bearing: only the archives on the current
    root->leaf path are live, which is what the live-bytes guard polices. Each archive's own
    meta.json tags the gallery IT produced. `RecursionBudget` = five guards (depth, total expanded,
    live bytes, nested count, expansion ratio), each failing only its BRANCH. Also
    `nested_gallery_name` / `unique_gallery_name`. Refuses unset hooks (a default std::function
    throws, and the project is exception-free).
  - `recursive_hooks.*` — the real backends: miniz for zip/cbz, ArchiveReader for the rest,
    MediaSink for galleries/placement/tags. `create_gallery` treats AlreadyExists as success
    (every plan lists every ancestor, so it is the normal case). The archive password is held in
    a `SecurePassword` = `shared_ptr<crypto::SecureBytes>` (via `make_secure_password`), shared by
    the list/extract closures and `crypto_wipe`'d when the last closure dies — NOT a plain
    std::string, which would leave the plaintext password in freed heap (invariant #2; security
    audit finding, PR #118). Backends still get a `string_view` over the buffer.
  - `recursive_exec.*` — `import_archive_recursive`, what ImportQueue's Zip/Archive tasks call.
    `sink_root` is REQUIRED, not defaulted: DirectVaultSink's base already includes the gallery
    name, StagingSink's does not, and guessing drops or duplicates a whole gallery level.
  - `volume_set.*` — `detect_volume_set` (pure over a sibling listing) + `assembly_for` +
    `concatenate_volumes`. Four styles; two ordering traps: a spanned zip's `.zip` is LAST though
    `.z01` sorts first, and old-style RAR's `.rar` is volume ONE despite sorting after `.r00`. A set
    is contiguous from its OWN minimum (7z starts 001, `split -d` starts 00). Two-volume minimum is
    what stops a lone `photos.zip` reading as a broken set.
  - `volume_import.*` — `summarize_volume_set` (pure; a gap is a REFUSAL, not a warning) +
    `assemble_volume_set` (reads volumes, applies `platform::normalize_user_path`, returns bytes
    for Concatenate/SpannedZipMerge or paths for FileOriented).
  - `spanned_zip.*` — `merge_spanned_zip`: strips the 4-byte spanning marker, rewrites every CD
    entry's disk number + local-header offset to absolute, fixes the EOCD. Disk-0 offsets must
    subtract the stripped marker or the CD enumerates perfectly and every extraction fails. Zip64
    spanned rejected (v1). Bounds-checked throughout.
  - `selectable.*` — `is_selectable` / `selectable_indices`, the ONE rule for what may enter a
    selection (image, video, gallery). Was inline in three places, which is how Ctrl+A and Space
    drift apart.
  - `volume_set_dialog.*` (SDL) — `VolumeSetDialog`: the confirm modal over a `VolumeSetSummary`.
    `Result{Pending,Confirmed,Cancelled}`; Enter/Y confirm, Esc/N cancel. Unlike ConsentDialog it
    has a state where confirming is NOT offered: `!can_import` makes Enter return `Pending`
    WITHOUT closing, so the user can read which volume is missing. Border ACCENT vs DANGER and the
    key hint both derive from `can_import`, so an unimportable set never advertises Enter. Long
    sets elide after 8 rows.
- `zip_plan.*` — pure ZIP placement planner: entries -> galleries to create + file placements +
  skip count. SDL-/miniz-/vault-free, unit-tested. `build_zip_plan` mirrors the archive tree 1:1;
  a dir holding both media and subdirs maps onto a mixed gallery (Phase 46), so there is no
  conflict policy and no user prompt. `ZipDest{NewGallery,Append}`. Phase 53: `ZipPlan::nested`
  collects entries whose NAME claims an archive (routed away from `skipped_unsupported`), and the
  parent gallery is created even for a directory holding only an archive. `build_cbz_plan` never
  populates it — a comic archive is a flat run of pages.
  `is_supported_image_name` + `build_cbz_plan` -> a fixed one-leaf plan (gallery named after the
  archive) of every image entry, videos/other skipped+counted, subfolders flattened (basename
  collisions disambiguated by source dir), natural reading order. `find_meta_entry` — a
  top-level meta.json (ci, files only) excluded by every planner path.
- `zip_encoding.*` — `decode_zip_entry_name`: legacy (non-UTF-8) zip/cbz entry-name decoding.
  When a name lacks the UTF-8 flag (bit 11), decodes via a fixed 128-entry CP437->Unicode table
  (0x80-0xFF; 0x00-0x7F ASCII), unless the raw bytes already parse as valid UTF-8 (passed
  through). Shift_JIS/other double-byte out of scope (imports safely, mis-decoded as CP437).
  Pure, unit-tested; used by zip_import's read_entry_list.
- `zip_import.*` — ZIP/CBZ import executor: miniz reader -> mlock'd SecureBytes (one entry at a
  time, no temp file) -> Vault::add_image/add_video by `image::detect_format`.
  import_cbz reuses the per-entry path over build_cbz_plan (`.cbz` -> one
  page gallery, never extracted to disk). Lives in ui/ like export.* (deps vault+image). Hosted
  by GalleryGrid (`Z` key). Optional `ImportProgress*` (atomic total/done/cancel). A top-level
  meta.json seeds the created gallery's tags via `Vault::add_tag`, but the name is NOT applied
  by the importer: `peek_archive_meta` reads meta at file-pick time, GalleryGrid prefills the
  name popup with `meta_gallery_name(meta,stem)` (confirmed popup text is authoritative).
  1 MiB meta cap; malformed meta.json never blocks the import.
- `archive_reader.*` — ArchiveReader: thin wrapper over libarchive's streaming read API
  (`archive_read_open_memory` over an mlock'd buffer), whole-file gated `OSV_VENDORED_ARCHIVE`.
  `open()` does one forward pass building `entries()` (reuses ZipEntry from zip_plan.h,
  format-agnostic); `extract(index,out)` holds a FORWARD STREAM CURSOR (PR #124): the open
  stream stays positioned past the last extracted entry, so ascending extracts (the import
  loop's pattern) share one stream and a full import is a single pass — libarchive has no
  random access, and the old reopen-per-extract was O(n²) decompression (~150x slower than
  zip on a solid 7z). An index behind the cursor transparently reopens; ANY extract failure
  resets the cursor (per-call independence preserved); `stream_opens()` observable pins the
  contract in tests. Reader is now non-copyable/non-movable (destructor frees the handle).
  `MAX_ENTRY_BYTES=4 GiB` bomb guard checked against the declared size before allocating.
  `open_files(paths, passphrase)` (Phase 53) opens an ORDERED multi-volume set via
  `archive_read_open_filenames` — libarchive's RAR multivolume support only works file-oriented,
  because each volume carries its own header and concatenation truncates. Paths are NOT
  normalised here; `normalize_user_path` stays at the boundary where paths enter (volume_import).
- `archive_import.*` — import_archive/import_archive_cbz: mirrors zip_import's structure but
  backed by ArchiveReader, covering .7z/.rar/.tar(+.gz/.xz)/.cbr/.cb7/.cbt. Declared
  unconditionally; the .cpp branches internally on OSV_VENDORED_ARCHIVE, returning a graceful
  "not supported" outcome without it, so gallery_grid.cpp needs zero #ifdefs. GalleryGrid's
  `classify_archive_ext()` picks miniz vs libarchive backend + CBZ-style vs mirror/append plan
  purely from the extension.
- `meta_json.*` — pure archive meta.json parser (nlohmann/json, header-only): `parse_meta_json`
  (tolerant, exception-free) -> `ArchiveMeta{title_english,title_japanese,tags}`;
  `meta_gallery_name` (english->japanese->fallback; '/'->'_') + `meta_gallery_tags` (japanese
  title first, searchable). Unit-tested.

## Background import queue (Phase 50)
- `import_queue.*` — `ImportQueue`: lifecycle managed by App (owns one, destroyed on app shutdown).
  One worker jthread + decode pool (min(hw,4) threads). Per-file pipeline: read source → decode → encrypt → append chunks → stage IndexNode.
  Ordering: decode parallel, append+attach strictly in sequence via a resequencer (lookahead cap 8 items/256MiB).
  Methods: `enqueue` (any thread, refuse if stopped), `abort_and_flush` (idempotent), `begin_session` (clears stale state/flags),
  `set_exclusive` (inhibit until released). Worker stops gracefully on Vault::lock().
  **Phase 51:** `enqueue_folder(vault, folder_path, dest_gallery_path, progress)` enqueues an ImportTaskKind::Folder,
  mirroring `enqueue_files`. Multiple folder picks create multiple tasks (one per folder).
  **Phase 53:** `enqueue_volume_set(volumes, style, stem, dest, gallery_name, kind, password)` — Task gained
  `volumes` / `volume_style` / `volume_stem`. The whole ordered volume list must ride ON the task: the worker
  runs off a task SNAPSHOT, and a snapshot carrying only `source` silently imports the first fragment alone.
  Kind is detected from the set's STEM, never a volume filename — `whole.zip.00` has extension `.00`.
  `process_volume_set_task(task, sink, pw) const` is a separate method (extracted for cognitive complexity):
  assemble → route by `VolumeAssembly` → `merge_spanned_in_place` for spanned zips → the normal recursive import.
- `import_model.*` — pure, SDL-free queue model: `ImportTask` (id, kind, source, dest, gallery_name, optional archive password);
  `ImportTaskKind{Files, Zip, ArchiveZip, Archive7z, ArchiveRar, ArchiveTar, Folder}` (Phase 51);
  `ImportRecord{task, state, progress, error}`; `ImportQueueModel` (FIFO/reorder/cancel/drain).
  `footer_import_summary(records,lane_error)` — formats "Importing X 128/450 · 2 queued" for the footer.
  Unit-tested, no vault dep.
- `import_status_screen.*` — `NavKind::ToImportStatus`, opened via **`Shift+I`** or footer click. Shows:
  current item (name, progress bar, source kind), queued items (Ctrl+Up/Down reorder, Del cancel), finished/failed with outcomes.
  `C` clears finished entries. Lane-failure banner surfaces hard stops. Esc returns to previous screen.
  Free friend gate (exclusive-op guards): blocks delete/transfer/combine/compact when queue is non-empty.
  **Phase 56:** Selection is now id-based (not positional index) so `Ctrl+Up`/`Ctrl+Down` keeps focus on the item it just moved.
  Each row is two lines — `source → destination` above, progress bar or outcome below — via `import_status_row.*` module.
  Summary modal geometry via `prompt_layout.*` module.
- `MediaSink` (Phase 50) — executor seam for add_image/add_video, unifying legacy `FileOpJob` path (now retired)
  and new queue path. Abstract `struct MediaSink` with `add_image(Vault,...)` and `add_video(Vault,...)` virtuals;
  `DirectVaultSink` calls vault directly (synchronous); `test_only_*` seam for testing.
  **Phase 53:** gained a `tag_gallery(vault, gallery_path, tag)` PURE VIRTUAL — deliberately not a defaulted
  no-op, because that is exactly how `StagingSink` silently dropped every meta.json tag on queued imports
  while the synchronous path kept working. `StagingSink`'s `StagedRecord` gained a `tag` field so tags survive
  until the main-thread drain applies them.
- **Retired:** `ZipImportJob` (entire class), `FileOpJob::start_import` (method); executors (zip_import, archive_import, etc.)
  reused by queue unchanged. GalleryGrid `Z` key now enqueues instead of launching a legacy job.

## Workers (Phase 51)
- `process_folder_task(Vault, task, sink, progress)` — mirror of `process_archive_task`, executes an
  ImportTaskKind::Folder: `scan_folder(task.source)` -> archive-style ZipEntry list -> `build_zip_plan` -> stage each
  file (decrypt-to-memory, no temp file) -> attach gallery tree. Bytes read into mlock'd SecureBytes.
  Main-thread-only invariant: tree untouched by worker (attach/commit only on queue drain).

## Layout modules — pure geometry helpers (Phase 56 extractions)
- `text_metrics.{h,cpp}` — font-derived text pitch: `line_pitch(font_px)` → `ceil(font_px * 1.25)` and `row_height(font_px, pad)` → `line_pitch + 2 * pad`. The 1.25 leading guarantees each pitch exceeds the font height, so adjacent lines cannot touch and a whole-line clip band cannot cut a descender. Pure, unit-tested; the SINGLE source of truth for all text-line pitches.
- `detail_layout.{h,cpp}` — detail-panel line layout. Pure, unit-tested. Used by `detail_panel.*`.
- `prompt_layout.{h,cpp}` — centred prompt/summary box geometry (used by tag overview edit prompt and import-summary modal). Pure, unit-tested. Used by `tag_overview.*` and `import_status_screen.*`.
- `list_layout.{h,cpp}` — shared vertical-list geometry for five surfaces (advanced search, saved-search panel, search result view, search overlay, tag editor). Pure, unit-tested. Encapsulates all list row heights and scrolling maths for these screens.
- `import_status_row.{h,cpp}` — Import Status two-line row formatting: `format_task_route(task)` → `"name → dest"`, `format_task_status(task)` → state string, `import_row_height(font_px, pad)` → `2 * pitch + 2 * pad`. Pure, unit-tested.
- `album_rebind.{h,cpp}` — Viewer album binding logic: when the vault tree changes, remember the current item's path, re-list the album, then look up the remembered path. Returns whether the item survives and whether to preserve zoom/pan/playback state. Pure, unit-tested. Used by `ImageViewer::on_vault_changed`.

## Pure view / sort / model helpers (SDL-free unless noted, all unit-tested)
- `child_counts.*` (Phase 51) — `direct_child_counts(node) -> SubtreeCounts` (galleries, images, videos counted
  separately, reusing the existing SubtreeCounts struct); `format_tile_counts(counts) -> string` — plural-aware
  formatting ("3 galleries · 12 items" / "1 gallery · 1 item" / "12 items" / "empty"), collapsing images+videos
  to "items". Counts reserved per gallery listing (never per tile); cell does not grow; label moves up,
  thumbnail shrinks by row height, leaving grid metrics and hit-testing untouched.
- `gallery_view.h/.cpp` — `GalleryView{List,GridS,GridM,GridL,GridXL}` shared enum;
  `cell_size_for(view)` (S=128/M=188/L=248/XL=320, List unused) + `next_gallery_view(view)`
  (the `L`-key cycle). GridM==188 matches the old fixed CELL. `gallery_view.cpp` is listed
  explicitly (not globbed) in osv_tests' premake5.lua files{}.
- `gallery_session_state.h` — `GallerySessionState{view,strip_side,detail_open,last_media_path,
  video_resume_seconds}` + `last_index_by_path` (unordered_map, key=NavModel::path()) +
  record(path,index)/recall(path) (0 default) + reset(). Phase 48: added `bool detail_open`,
  persisted across screen transitions within a session. App-owned; App writes most fields once
  at screen exit, but GalleryGrid writes `last_index_by_path` repeatedly during its lifetime.
- `nav_model.*`, `input.*`, `viewer_model.h`, `screen.h` — navigation model, input handling,
  viewer model, Screen base (with `help_groups()` virtual overridden by GalleryGrid,
  ImageViewer, FavoritesScreen, TagOverviewScreen, AdvancedSearchScreen, VaultManager,
  UnlockScreen).
- `natural_sort.*` — natural-order name comparator (`natural_compare` 3-way: digit runs by
  value so "2"<"10", other chars ci, fewer leading zeros first; `natural_less`). Orders CBZ
  pages by reading order.
- `gallery_sort.*` — per-gallery sort presentation: `sort_children(children,SortKey)` — folders
  always precede media, then Default/Insertion are no-ops / NameAsc,Desc delegate to
  natural_less / Date* compares created_ts / Size* compares orig_size (all stable).
  Phase 49: `next_sort_key` cycles all EIGHT values
  (Default→NameAsc→NameDesc→DateAsc→DateDesc→SizeAsc→SizeDesc→Insertion→Default) and
  `prev_sort_key` is its exact inverse (round-trip unit-tested), so a settings row bound to
  left/right arrows moves symmetrically. `effective_sort_key(gallery_key, vault_default)`
  resolves a gallery's stored key against the vault-wide default and NEVER returns `Default`,
  so its result is always safe for sort_children; a vault default that is itself `Default`
  (only reachable from a hand-edited blob) degrades to `Insertion`. `sort_key_label(key,
  vault_default)` takes TWO arguments and is empty ONLY for a gallery at `Default` in a vault
  whose default is `Insertion` — i.e. a vault nobody configured, which must look exactly as it
  did pre-Phase-49. Used by `Vault::list` (which resolves through effective_sort_key) and
  GalleryGrid's footer/HUD.
- `tag_category.*` (Phase 49, pure) — `resolve_tag(tag, categories) -> TagDisplay{string_view
  text; int swatch}`. Splits a stored tag at the FIRST ':' and strips the prefix only when it
  matches a configured category (case-insensitively, via `ui::tag_ci_equal`) AND the suffix is
  non-empty — so `12:30`, `Re:Zero` and `artist:` survive verbatim. `swatch < 0` means
  uncategorised (drawn verbatim in TEXT_DIM). **`TagDisplay::text` BORROWS from the caller's
  `tag` argument** — never resolve a temporary.
- `tag_chip.*` (Phase 49) — the single home of chip geometry: `CHIP_DOT` 9, `CHIP_GAP` 7,
  `CHIP_SPACING` 12, `CHIP_ROW_H` 16, used by five surfaces and defined nowhere else. Pure,
  unit-tested: `fit_chips` (how many fit + a "+N" overflow count), `chip_width`,
  `lone_chip_text_w` (text room for a single chip that cannot fit, so it middle-elides instead
  of collapsing to a bare "+1"), `pack_chip_lines` (+ `ChipLine`/`ChipWrap`; wraps a run across
  at most `max_lines` — two passes: tight when everything fits, else every line repacked into
  `max_w - CHIP_SPACING - overflow_w` so a RIGHT-ALIGNED "+N" can never collide; when nothing
  fits `lines` is empty and callers must NOT assume `lines.back()` exists), and
  `any_chips_to_show(children)` (per-GALLERY reservation predicate — deliberately ignores the
  category list, since an uncategorised tag still draws). Drawing: `draw_tag_chips(r, font, x,
  y, max_w, tags, categories)` where `y` is the row TOP and content is centred within
  CHIP_ROW_H via `font.text_top_for_center`. A `static_assert` in the .cpp ties
  `gfx::TAG_SWATCH_COUNT` to `vault::TAG_SWATCH_COUNT` — they are separate constants because
  gfx must not depend on vault, and this is the one TU where both are visible.
- `settings_model.*` (Phase 49, pure, SDL-free) — state behind the `F2` overlay. **Phase 54:
  `SettingsState::prompt_buf` is a `TextInputModel`, so `SettingsState` is now non-copyable
  and non-movable** (every use was already by reference; only the test fixture changed, and
  `state = {}`-style resets are not available). Contents:
  `SettingsSection{Appearance,Browsing,TagColours}` + `SETTINGS_SECTION_COUNT`, and
  `SettingsState{section,in_pane,row,open,vault_unlocked,draft,theme,prompting,prompt_row,
  prompt_buf,error}`. `settings_move_section` (clamps, resets row), `settings_move_row`
  (clamps), `settings_change_value` (theme wraps both ways; default sort steps via
  next_/prev_sort_key SKIPPING `Default`, which is meaningless as a vault default; tile flag
  toggles; a category row wraps its swatch mod 16), `settings_row_count` (Appearance always 1;
  the two vault sections 0 unless `vault_unlocked`), and category CRUD
  `settings_add_category`/`settings_rename_category`/`settings_remove_category` (trim, reject
  blank/duplicate via `ui::tag_ci_equal`, honour `INDEX_MAX_CATEGORY_BYTES`/
  `INDEX_MAX_TAG_CATEGORIES`). NOTE: the CRUD/value entry points do NOT themselves check
  `vault_unlocked` — they are memory-safe either way, so the OVERLAY must not route value keys
  into a locked vault's sections.
- `settings_overlay.*` (Phase 49, SDL) — draws the overlay (veil + section rail + row pane +
  footer) and handles its input: `open_settings`, `close_settings(state, window)`,
  `handle_settings_event(state, window, event, commit_out)` (takes the full SDL_Event, not a
  keycode, because the add/rename prompt needs SDL_EVENT_TEXT_INPUT and
  SDL_StartTextInput/StopTextInput need a Window), and `draw_settings_overlay`. Theme rows
  apply live and persist exactly as the retired ThemePicker did. Not in `osv_tests`.
- `listing_remap.*` (Phase 58) — `ui::remap_listing(before, after, import_dict) ->
  ListingRemap` — preserve gallery scroll and multi-selection across drain batches by remapping
  selection by name (not tile identity/path). Import flows selection/multi-selection through
  `import_dict` (vault path -> node name), fills `child_names_` (the single fill site for
  GalleryGrid::child_names_), and hands a remap to detect re-exports (same path) vs
  no-ops (nothing exported) vs new children (added names) — grid can now skip unnecessary
  re-renders. Returns `child_names_` and selection keyed by name for the grid to reestablish.
- `search_model.*` — pure query tokenise/match/rank; drives the `/` overlay's live filter+rank.
  tokenize/matches reused by GalleryPickerModel.
- `advanced_search_model.*` — pure advanced query: `AdvancedQuery{weighted include (OR gate +
  scorers), exclude (hard filter), AND/OR TagGroups + top-level join, name substring,
  SearchScope}`; `evaluate()`->{matched,score}; serialize/deserialize (opaque blob);
  `tag_suggestions(prefix,vocab)` ranked autocomplete. vault.cpp includes it (one-way dep) for
  run_search. **Phase 58:** SearchOverlay split gather (vault walk → cached hits) from filter
  (per-keystroke predicate over hits). `gather_results(vault, scope)` walks the index once per
  open/scope-change/vault-change and caches `SearchHit`s (dangling `IndexNode*` refs fixed on
  `on_vault_changed()` — now public, called by GalleryGrid on drain; AdvancedSearchScreen calls
  it too on same event). `filter_results` runs per keystroke over the cache (no vault I/O).
  Eliminates per-keystroke 50k-item index walk.
- `debounce.h` (Phase 58) — header-only `Debounce<Fn>` timer: call `maybe_run(dt)` and it runs
  `fn` after `idle_threshold` seconds of elapsed `dt` without a `reset()` (keystroke fires reset).
  Used by AdvancedSearchScreen to debounce rerun to 150 ms input silence (simpler than immediate
  per-keystroke walks on large result sets). Other rerun sites (simple/search overlay) use immediate
  (no debounce). Debounce flushes (immediate run) before result-open and saved-search save to avoid
  stale display.
- `search_result_view.*` / `result_grid.*` — result grid+list view state (`ResultView{List,
  Grid}` + toggle + move nav; List ±1 row, Grid ±1/±cols clamped, cols>=1). search_result_view
  owns the off-thread decode worker + feeds the thumbnail cache. **Phase 56:** list layout
  derives from `list_layout.*` module. **Phase 58:** `update_results(vault, hits)` invalidates
  CoverCache and clears failed thumbs.
- `saved_search_panel.*` — saved-search sidebar: list rendering + CRUD (Ctrl+S/Enter/Del). Pure
  vault/SDL-free. Phase 54: `save_buf_` is a `TextInputModel` and `active_buffer()` returns
  `ITextInput*`; before that this field had NO Backspace handler at all, so a typo could only
  be undone by cancelling the save (regression-tested now). **Phase 56:** list layout derives from `list_layout.*` module.
- `tag_suggest.*` — pure autosuggest source: `editor_tag_suggestions(buffer,vocab,own_tags)` —
  trim, rank, hide own tags, cap `TAG_SUGGEST_MAX=5`.
- `tag_inherit.*` — ancestor-gallery tag union: `inherited_tags(vault,node_path)` — root→parent
  order, ci de-dupe, minus own tags. Feeds the tag editor's read-only section.
- `tag_contents.*` (Phase 51) — descendant-gallery tag union: `contents_tags(vault, gallery_path) -> vector<string>`
  read-time only, depth-bounded by INDEX_MAX_DEPTH, nothing stored. Mirrors `inherited_tags` structure: ci de-dupe,
  minus own and inherited tags (return empty if the node is not a gallery or is a leaf). Feeds the tag editor's
  read-only "From contents" section and the detail panel's "From contents" tag row.
- `tag_list_parse.*` — `parse_tag_list(span)` -> normalised tags (split LF, trim, drop blanks,
  ci de-dupe keeping first casing, `TAG_MAX_BYTES=0xFFFF`, cap INDEX_MAX_TAGS; non-UTF-8
  opaque). GalleryGrid `Shift+G` on a gallery tile opens a .txt dialog -> add_tag each (merge).
- `tag_json_parse.*` (Phase 55) — `parse_tag_dict_json(span) -> TagDictParseResult{entries,
  malformed_skipped, fields_truncated, over_cap_skipped}`. Exception-free nlohmann
  (`allow_exceptions=false`, guards, no try/catch — the `meta_json.cpp` pattern). Accepts a
  bare array OR `{"tags":[…]}`. Per entry: `TagDictEntry{category,name,description}` +
  `key()` (`"cat:name"` or bare `name`). Trims every field; `name` REQUIRED and must not
  contain `:` (resolve_tag splits on the first colon — rejected loudly, counted malformed);
  category/description truncated to `INDEX_MAX_CATEGORY_BYTES`/`INDEX_MAX_TAG_DESC_BYTES`
  on a UTF-8 boundary (via `utf8_prev_boundary`) and counted in `fields_truncated` — a
  truncated entry is still IMPORTED, never counted as skipped; ci de-dupe on `key()` within
  the file (first casing + first description win); entry count capped at
  `INDEX_MAX_TAG_DESCRIPTIONS` (`over_cap_skipped`). Pure — no vault, no SDL, no disk.
- `tag_dict_import.*` (Phase 55) — `apply_tag_dict(vault::VaultSettings&,
  const TagDictParseResult&) -> TagDictImportSummary`. Pure over the SETTINGS struct (never a
  Vault), which is what makes the whole import unit-testable unopened; the CALLER does the
  single `set_vault_settings` commit. New category → lowest palette index no category uses,
  wrapping `size % TAG_SWATCH_COUNT` once all 16 are taken; an existing category is NEVER
  recoloured. Descriptions upserted via `vault::set_tag_description`; an EMPTY description
  leaves an existing one intact — deliberate divergence from Phase 51's edit-time "empty
  removes" (an empty field in a file is ambiguous). Identical text counts as neither added
  nor updated. `INDEX_MAX_TAG_CATEGORIES` is checked HERE (only the applier knows the vault's
  count) → `categories_skipped_over_cap`, description cap → `entries_skipped_over_cap`; the
  entry's description still lands when only the category was refused. `tag_dict_summary_lines`
  is the pure modal body: three "what changed" counts always, each non-zero skip/truncation
  on its own line — no cap is ever silent.
- `tag_overview_model.*` — `TagTally{tag,gallery_count,image_count,description}` (Phase 51) +
  sort_tags(Name/Count) + filter_tags(ci prefix).
- `detail_model.*` (Phase 48) — pure detail-panel content: `DetailRow`/`DetailSection`/
  `DetailContent` + `build_node_details(node, inherited, vault_default)` (image/video/gallery
  field sets, own+inherited tag sections; the trailing `vault::SortKey` came in with Phase 49
  because this builder is pure by design and cannot look the vault default up itself — there
  is deliberately NO defaulted parameter or 1-arg overload, which is exactly how the breadcrumb
  and the panel would silently drift apart). **Phase 51:** gained `from_contents` parameter with
  NO default and NO overload (deliberate — a default is how the panel and the editor would
  silently drift); "From contents" emitted for galleries only, non-empty only, is_tags=true so
  the existing chip renderer draws it. Plus `build_selection_details(nodes, inherited)` (aggregate counts,
  summed size, ci tag intersection, "no shared tags"). Delegates every string to meta_format;
  gallery totals via `count_subtree`. SDL-/gfx-free, unit-tested.
- `detail_panel.*` (Phase 48) — right-edge panel: `DetailPanelState{open,scroll}`,
  pure `detail_panel_width(open,window_w)` (0 when closed OR window < 640 px),
  `draw_detail_panel(..., categories)` (returns content height for scroll clamping; culls to
  rect, fit_text-elides every vault string; Phase 49 added the trailing
  `std::span<const vault::TagCategory>` and renders `is_tags` sections as one-tag chip runs at
  CHIP_ROW_H instead of "• text" at ROW_H — the three hosts pass
  `vault::vault_settings(vault_).categories`), `handle_detail_panel_scroll` (Ctrl+Up/Down) + pure `detail_panel_hit(open,window_w,mouse_x)`
  (region derived from detail_panel_width, so it cannot disagree with the reserved strip) and
  `scroll_detail_panel(st,wheel_y)` (clamps at 0 only; the host applies the upper clamp).
  **Phase 56:** line layout derives from `detail_layout.*` module.
  Hosted by GalleryGrid, FavoritesScreen (covers all 4 subclasses), AdvancedSearchScreen.
- `compact_album.*` (Phase 58) — `compact_album(vault, root_node_path) -> vector<IndexNode*>` —
  flattened list of media nodes for collection-mode viewers (favorites, tag overview, search
  results). Re-resolves `IndexNode*` pointers from paths to survive drains (import batches).
  Called on every viewer `on_vault_changed`, so collection viewers' album stays valid across
  vault mutations. Delegates to `vault::resolve_node` (path-safe). Fixes dangling pointers that
  caused playback/selection reset during imports.
- `anim_repair.*` (Phase 47; was `gif_repair.*`) — `maybe_repair_animated(...)` +
  `Vault::repair_image_animated(path,bool)`: lazy bidirectional healing for images stored before
  their format's animation support landed, persisted via the same crash-safe `commit_index()`
  path as video repair. No-op when the animated flag is already correct. Gated on
  `vault::format_can_animate`, so it covers GIF and WebP — though the WebP arm is unreachable for
  pre-Phase-57 vaults (an animated WebP could not be imported at all, so no such node exists).
  `AnimSniffGate` (PR #122) gates the viewer's sniff, which costs a full image read+decrypt:
  `should_sniff(node)` is true only for an animatable image whose animated flag is UNSET (a set
  flag is trustworthy — import and repair both persist the value sniffed from actual bytes), at
  most once per `data_offset` per gate lifetime. ImageViewer holds one per session
  (`anim_sniff_gate_`); before this, EVERY navigation onto any GIF re-read the whole image.
- `video_repair.*` — `repair_unknown_video_metadata(vault,gallery_path,children)` sweeps a
  freshly listed gallery for videos still at `VideoCodec::Unknown` + calls
  `Vault::repair_video_metadata` per node. Called from GalleryGrid::refresh() so previously-
  imported videos self-heal (thumbnail+duration) on next open — no migration.
- `strip_layout.*` — orientation-aware viewer-strip geometry + half-size thumbnails.
  Phase 47: `strip_cell_rect(...)` added for forward index→rect mapping (inverse `strip_hit_axis`
  pre-existed). NOTE: `gfx::Renderer::draw_thumbnail_strip` duplicates this layout internally
  (gfx must not depend on ui) — both sites carry SYNC comments; keep in sync on geometry changes.
- `scroll_model.*` — fill-width continuous-scroll maths.
- `chrome_layout.*` — pure `ChromeBands{header,content,footer}` + `split_chrome(area,header_h,
  footer_h)`: reserves OPAQUE fixed bands at the top/bottom of `area` and hands back the content
  area between them. The three rects tile `area` exactly (unit-tested invariant); negative
  heights clamp to 0 and oversized bands shrink proportionally so content never inverts. The
  contract is *reserve, don't overlay* — a translucent band over scrolling/zooming content is
  both unreadable and hides what it covers. Used by GalleryGrid (header to OY + `FOOTER_H`=48
  status band) and ImageViewer (`viewer_chrome`). Draw side: `ui::draw_chrome_band` in
  `widgets.*` (opaque fill + a BORDER hairline on the content-facing edge; no-ops on a
  collapsed band).
- `meta_format.*` — list-view metadata formatting: size/dimensions/date/type. Phase 48: added
  `video_container_name(vault::VideoContainer)` -> "MP4"/"MKV"/"-" for unknown.
  `video_codec_name(vault::VideoCodec)` is a `static constexpr std::array` table + `static_assert`
  pinning its size to the enum's last value, not a switch (same pattern and rationale as
  `media::map_codec_id` — see `mem:module/media`): adding a `VideoCodec` enumerator without a
  display name is a build error. Unmapped/`Unknown` falls back to "Video".
- `delete_summary.*` — recursive tally of a gallery subtree (images/videos/sub-galleries) +
  plural-aware format for the Del confirm popup. Phase 48: `SubtreeCounts` gained `uint64_t bytes`,
  summing descendant `orig_size`. GalleryGrid Del removes the focused image/video
  (`Vault::remove_image`) or gallery subtree (`Vault::remove_gallery`) behind the modal.
- `gallery_cover.*` — cover resolution (walks index tree -> thumb chunk spans only):
  `resolve_single_cover` (leaf: first image thumb / first video poster; non-leaf: recurse first
  sub-gallery) + `resolve_covers` (non-leaf: up to 4 sub-gallery covers in child order).
  Depth-bounded by INDEX_MAX_DEPTH, cycle-free. No decode, no disk. **Phase 58:** extracted to
  pure function (`resolve_single_cover`, `resolve_covers`), no cache internal.
- `cover_cache.*` (Phase 58) — `CoverCache{spans_by_node}` — gallery cover spans computed once
  per listing and keyed by `IndexNode*`. Eliminates per-frame re-resolution cost on grid refresh.
  Hosts (GalleryGrid::refresh, SearchResultView::update_results, import drains) MUST invalidate
  on gallery refetch (no auto-invalidation). GalleryGrid also clears `thumbs_.failed` on refresh
  so failed reads are retried on new gallery list (not cached stale).
- `cover_layout.*` — `cover_montage_rects` (tile rect + 1–4 covers -> sub-rects; single fill
  for 1, row-major 2×2 for 2–4).
- `tile_thumb.*` — shared tile-thumbnail draw: `ThumbContext{vault,cache,worker,failed}` +
  draw_tile_thumb / tile_thumb_texture / tile_cover_tex. Gallery -> folder + cover montage;
  image -> aspect-fit thumb; video -> poster + play-badge. Phase 47: `tile_shows_animated_badge(node)`,
  `draw_animated_badge(...)` draw an "A" badge top-right for animated images (GIF, plus WebP
  since Phase 57 — both via `vault::format_can_animate`, so a stale flag on a non-animatable
  format never badges); `tile_can_hover_animate(node)` gates hover animation by badge +
  dimension budget. `thumb_key_for` pure index lookup.
  Decrypt -> off-thread decode -> GPU upload via shared cache. Reused by GalleryGrid + the
  advanced-search grid view. **Phase 58:** `ThumbKey{key, offset, length, present}` — cache
  identity (key) unchanged; offset/length span added. Render thread no longer does vault I/O
  for thumbs; `DecodeWorker::submit_fetch` wires a Fetcher to vault's `read_thumbnail()`. Read
  failures memoized as empty Result (like decode failures) instead of retried every frame;
  hosts clear `failed` on refetch.
- `waste_threshold.h` — vault-bloat thresholds: `should_display_waste(wasted,file_size)` (true
  if waste > max(50 MiB, 10% of file_size)); `should_hint_cancelled_import_waste(wasted)` (true
  if > 1 MiB). Drives GalleryGrid's `Shift+C` compact-confirm footer hint.
- `keybindings.h` — pure layout-independent key resolution: `bracket_key_for_scancode` maps the
  two physical keys right of `P` -> `BracketKey{Decrease,Increase}` by SDL SCANCODE (video
  volume `[`/`]` + slideshow dwell on any layout). Centralises the character-resolved
  `is_search_key`/`is_advanced_search_key`/`is_quick_switch_key` helpers.
