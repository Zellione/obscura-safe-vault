# Module: ui/ — screens, viewer, dialogs, pure models

Referenced from `mem:core`. Covers `src/ui/`: every Screen, the image/video viewer, all
modal dialogs, and the pure SDL-free view/search/sort/session models. Many free-friend
helpers exist purely to keep host Screens under the cpp:S1448 35-method cap.

## Screens
- `unlock_screen.*` — password + optional keyfile unlock. `secure_text_field.*` /
  `unlock_logic.*` back it (secure entry field + unlock logic). `passphrase.*` helpers.
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
  Phase 48: `content_width(const GalleryGrid&)` free friend is now the SINGLE layout-width
  source (window minus detail panel) — render(), scroll-to-selection, on_enter's cols_ seed,
  and hit_test all route through it; using win_.width() directly desyncs picking from drawing
  whenever the panel is open.
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
  `ToUnlock(path)`/`ToGallery`. `C` opens the ThemePicker overlay.
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
  the cache key.
- `tag_overview.*` — `Shift+T` first-class Screen (`NavKind::ToTagOverview`): scrollable tag
  list (Up/Down, Enter, Tab=toggle sort, type=prefix filter, `` ` ``=quick-switch); counts
  from `VaultSearch::tag_overview`; Enter -> TagGalleries.
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
  strip toggle (keys F/T, P starts slideshow). `widgets` has button_state + elide_middle +
  fit_text (elide_middle bound to a FontAtlas). Hosts a fit-only VideoPlayback when the current
  item `is_video()`: Space play/pause, J/L ±5s, `,`/`.` frame-step, drag seek bar; M mute,
  volume ∓5% (seek bar seeks video+audio in-sync). Volume via `ui::volume_dir` — `-`/`+` glyph
  keys (HUD `[-/+] Vol`) + `[`/`]` produced char resolved through `SDL_GetModState` (German
  AltGr) + physical bracket scancodes; level seeded from `media::saved_volume()` on open,
  written back on change. `R` toggles loop (process-lifetime `media::saved_loop_enabled()`;
  VideoPlayback re-seeks to 0 and keeps playing at EOF when set); on-screen ring indicator next
  to play/pause. Ctor gains `initial_strip_side` (default Bottom); three free friends —
  `current_strip_side`, `capture_video_resume` (snapshot outgoing viewer's video path+position
  into a GallerySessionState, or clear when the current item isn't a live video),
  `apply_video_resume` (seek a freshly (re)opened matching video to the remembered position,
  called right after `on_enter()` builds video_). "Collection mode" (explicit image set +
  per-image path + exit Nav) lets the viewer serve favorites/tag sets, not just one gallery.
- `gif_playback.*` (Phase 47, gated `OSV_VENDORED_AV`) — `GifPlayback`: pImpl, FFmpeg
  confined to `.cpp` so `image_viewer.h` compiles everywhere. Auto-loop, Space toggles pause,
  zoom/pan unchanged. Decrypted bytes held in mlock'd `crypto::SecureBytes` outliving the
  decoder. Frames uploaded row-by-row honoring `SDL_LockTexture` pitch.
- `gif_model.*` (Phase 47) — pure logic: `GifHoverGate` (200 ms dwell, one start-edge per
  hover), `gif_within_hover_dimension_budget(w,h)`, `gif_hover_frame_count_exceeded(frames)`,
  `gif_frames_to_advance(...)` with 64-frame catch-up cap.
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
  extra row appended after filtering, exempt from the filter.
- `rename_dialog.*` — `R` modal, renames the focused image/video/gallery via
  `vault::rename_node`. open(gallery_path,old_name)/close()/handle_event/render/
  consume_completed(status_out).
- `tag_editor.*` — `G` add/remove-tags modal in GalleryGrid + ImageViewer. Current-tags list
  scrolls (Up/Down) via pure `tag_scroll.h` + auto-scrolls to a just-added tag. Read-only
  "Inherited from gallery" section (`ui::inherited_tags`, `tag_inherit.*`) below own-tags;
  Del/selection never touch it. Autosuggest dropdown while typing (`VaultSearch::all_tags`
  vocab refreshed on open/add/remove; `ui::editor_tag_suggestions`; Up/Down highlight via
  move_tag_cursor, -1=buffer; Enter adds the TYPED text unless a suggestion is highlighted; Esc
  deselects first).
- `search_overlay.*` — `/` live-filtered search modal in GalleryGrid; Tab cycles scope
  (Both/Images/Galleries).
- `consent_dialog.*` — modal "export anyway?" confirm (SDL plumbing).
- `theme_picker.*` — `C` overlay over vault manager (QuickSwitch-style): Up/Down previews a
  built-in theme live (`gfx::set_theme`) + persists it (`platform::ThemePref`) — the preview IS
  the choice; Enter/Esc close. Each row shows an accent swatch.
- `quick_switch.*` — global `` ` `` (grave) overlay: lists registry vaults; choosing one emits
  `NavKind::ToUnlock(path)` (App locks current + unlocks chosen); Esc or the active vault =
  no-op. Hosted by GalleryGrid, ImageViewer, FavoritesScreen base (take a VaultRegistry& +
  active vault path). `consume_choice()` drains the pick.
- `progress_modal.*` — `draw_op_progress`: shared veil + "N/M" bar + cancel-hint modal reused
  by every screen hosting a background job.
- `help_popup.*` — shared `F1` help popup: HelpGroup/HelpEntry types + pure open/close/scroll
  logic + `draw_help_popup`. `Screen::help_groups()` virtual (default empty) supplies
  per-screen grouped content; App owns HelpPopupState + intercepts F1 globally. Esc/Q close.

## Export (the one gated invariant-#1 deviation)
- `selection_model.*` — multi-select state for export (pure/tested). Phase 48: gained
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
- `zip_plan.*` — pure ZIP placement planner: entries -> galleries to create + file placements +
  skip count. SDL-/miniz-/vault-free, unit-tested. `build_zip_plan` mirrors the archive tree 1:1;
  a dir holding both media and subdirs maps onto a mixed gallery (Phase 46), so there is no
  conflict policy and no user prompt. `ZipDest{NewGallery,Append}`.
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
  format-agnostic); `extract(index,out)` re-opens a fresh stream + walks forward to index EACH
  call (libarchive has no random access) — O(n) per extract, fine for gallery-sized archives.
  `MAX_ENTRY_BYTES=4 GiB` bomb guard checked against the declared size before allocating.
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

## Pure view / sort / model helpers (SDL-free unless noted, all unit-tested)
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
  always precede media, then Manual is a no-op / NameAsc,Desc delegate to natural_less / Date*
  compares created_ts / Size* compares orig_size (all stable). `next_sort_key` cycles the fixed
  Shift+S order (Manual→NameAsc→NameDesc→DateAsc→DateDesc→SizeAsc→SizeDesc→Manual);
  `sort_key_label` (empty for Manual). Used by both Vault::list and GalleryGrid's footer/HUD.
- `search_model.*` — pure query tokenise/match/rank; drives the `/` overlay's live filter+rank.
  tokenize/matches reused by GalleryPickerModel.
- `advanced_search_model.*` — pure advanced query: `AdvancedQuery{weighted include (OR gate +
  scorers), exclude (hard filter), AND/OR TagGroups + top-level join, name substring,
  SearchScope}`; `evaluate()`->{matched,score}; serialize/deserialize (opaque blob);
  `tag_suggestions(prefix,vocab)` ranked autocomplete. vault.cpp includes it (one-way dep) for
  run_search.
- `search_result_view.*` / `result_grid.*` — result grid+list view state (`ResultView{List,
  Grid}` + toggle + move nav; List ±1 row, Grid ±1/±cols clamped, cols>=1). search_result_view
  owns the off-thread decode worker + feeds the thumbnail cache.
- `saved_search_panel.*` — saved-search sidebar: list rendering + CRUD (Ctrl+S/Enter/Del). Pure
  vault/SDL-free.
- `tag_suggest.*` — pure autosuggest source: `editor_tag_suggestions(buffer,vocab,own_tags)` —
  trim, rank, hide own tags, cap `TAG_SUGGEST_MAX=5`.
- `tag_inherit.*` — ancestor-gallery tag union: `inherited_tags(vault,node_path)` — root→parent
  order, ci de-dupe, minus own tags. Feeds the tag editor's read-only section.
- `tag_list_parse.*` — `parse_tag_list(span)` -> normalised tags (split LF, trim, drop blanks,
  ci de-dupe keeping first casing, `TAG_MAX_BYTES=0xFFFF`, cap INDEX_MAX_TAGS; non-UTF-8
  opaque). GalleryGrid `Shift+G` on a gallery tile opens a .txt dialog -> add_tag each (merge).
- `tag_overview_model.*` — `TagTally{tag,gallery_count,image_count}` + sort_tags(Name/Count) +
  filter_tags(ci prefix).
- `detail_model.*` (Phase 48) — pure detail-panel content: `DetailRow`/`DetailSection`/
  `DetailContent` + `build_node_details(node, inherited)` (image/video/gallery field sets,
  own+inherited tag sections) + `build_selection_details(nodes, inherited)` (aggregate counts,
  summed size, ci tag intersection, "no shared tags"). Delegates every string to meta_format;
  gallery totals via `count_subtree`. SDL-/gfx-free, unit-tested.
- `detail_panel.*` (Phase 48) — right-edge panel: `DetailPanelState{open,scroll}`,
  pure `detail_panel_width(open,window_w)` (0 when closed OR window < 640 px),
  `draw_detail_panel(...)` (returns content height for scroll clamping; culls to rect,
  fit_text-elides every vault string), `handle_detail_panel_scroll` (Ctrl+Up/Down) + pure `detail_panel_hit(open,window_w,mouse_x)`
  (region derived from detail_panel_width, so it cannot disagree with the reserved strip) and
  `scroll_detail_panel(st,wheel_y)` (clamps at 0 only; the host applies the upper clamp).
  Hosted by GalleryGrid, FavoritesScreen (covers all 4 subclasses), AdvancedSearchScreen.
- `gif_repair.*` (Phase 47) — `maybe_repair_gif_animated(...)` + `Vault::repair_image_animated(path,bool)`:
  lazy bidirectional healing for GIFs stored before Phase 47, persisted via the same crash-safe
  `commit_index()` path as video repair. No-op when the animated flag is already correct.
- `video_repair.*` — `repair_unknown_video_metadata(vault,gallery_path,children)` sweeps a
  freshly listed gallery for videos still at `VideoCodec::Unknown` + calls
  `Vault::repair_video_metadata` per node. Called from GalleryGrid::refresh() so previously-
  imported videos self-heal (thumbnail+duration) on next open — no migration.
- `strip_layout.*` — orientation-aware viewer-strip geometry + half-size thumbnails.
  Phase 47: `strip_cell_rect(...)` added for forward index→rect mapping (inverse `strip_hit_axis`
  pre-existed). NOTE: `gfx::Renderer::draw_thumbnail_strip` duplicates this layout internally
  (gfx must not depend on ui) — both sites carry SYNC comments; keep in sync on geometry changes.
- `scroll_model.*` — fill-width continuous-scroll maths.
- `meta_format.*` — list-view metadata formatting: size/dimensions/date/type. Phase 48: added
  `video_container_name(vault::VideoContainer)` -> "MP4"/"MKV"/"-" for unknown.
- `delete_summary.*` — recursive tally of a gallery subtree (images/videos/sub-galleries) +
  plural-aware format for the Del confirm popup. Phase 48: `SubtreeCounts` gained `uint64_t bytes`,
  summing descendant `orig_size`. GalleryGrid Del removes the focused image/video
  (`Vault::remove_image`) or gallery subtree (`Vault::remove_gallery`) behind the modal.
- `gallery_cover.*` — cover resolution (walks index tree -> thumb chunk spans only):
  `resolve_single_cover` (leaf: first image thumb / first video poster; non-leaf: recurse first
  sub-gallery) + `resolve_covers` (non-leaf: up to 4 sub-gallery covers in child order).
  Depth-bounded by INDEX_MAX_DEPTH, cycle-free. No decode, no disk.
- `cover_layout.*` — `cover_montage_rects` (tile rect + 1–4 covers -> sub-rects; single fill
  for 1, row-major 2×2 for 2–4).
- `tile_thumb.*` — shared tile-thumbnail draw: `ThumbContext{vault,cache,worker,failed}` +
  draw_tile_thumb / tile_thumb_texture / tile_cover_tex. Gallery -> folder + cover montage;
  image -> aspect-fit thumb; video -> poster + play-badge. Phase 47: `tile_shows_animated_badge(node)`,
  `draw_animated_badge(...)` draw an "A" badge top-right for animated GIFs; `tile_can_hover_animate(node)`
  gates hover animation by badge + dimension budget. `thumb_key_for` pure index lookup.
  Decrypt -> off-thread decode -> GPU upload via shared cache. Reused by GalleryGrid + the
  advanced-search grid view.
- `waste_threshold.h` — vault-bloat thresholds: `should_display_waste(wasted,file_size)` (true
  if waste > max(50 MiB, 10% of file_size)); `should_hint_cancelled_import_waste(wasted)` (true
  if > 1 MiB). Drives GalleryGrid's `Shift+C` compact-confirm footer hint.
- `keybindings.h` — pure layout-independent key resolution: `bracket_key_for_scancode` maps the
  two physical keys right of `P` -> `BracketKey{Decrease,Increase}` by SDL SCANCODE (video
  volume `[`/`]` + slideshow dwell on any layout). Centralises the character-resolved
  `is_search_key`/`is_advanced_search_key`/`is_quick_switch_key` helpers.
