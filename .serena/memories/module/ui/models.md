# Module: ui/models — pure layout, view, search, and data models

Pure SDL-free view/sort/model helpers, layout geometry, settings state, search infrastructure.

## Export & batch operations (refactored from old "Export" section)
- `selection_model.*` — multi-select state for export AND batch ops (Phase 68:
  the same Space/Ctrl+A selection drives B favorite-toggle, X export, and M
  move/copy on the grid, the favorites/tag screens, and the search-result
  panel). Phase 53 adds
  `select_all(count)` / `all_selected(count)` behind Ctrl+A; `select_all(0)` does NOT clear
  ("select all of nothing" is not a deselect) and `all_selected(0)` is false (else Ctrl+A on an
  empty gallery clears forever). Phase 48: gained
  `revision()`, a monotonic counter incremented on `toggle()` and `clear()`, used as a cache
  key by the detail panel.
- `export_ui.*` — shared consent + folder-pick plumbing used by gallery + viewer.
- `export.*` — decrypt→write-verbatim→wipe export (SDL-free/tested). The ONE deliberate
  deviation from invariant #1: writes decrypted originals to disk on explicit user consent
  (selection-only, never thumbnails, buffer wiped right after write).
- `favorite_batch.*` — `batch_favorite_target(nodes)` (pure/tested): the batch
  favorite rule — any node unfavorited → favorite all (true), else unfavorite
  all. Persisted by `vault::set_favorites_batch` (one commit). Used by the
  grid's `toggle_favorite_selection` free friend, FavoritesScreen, and the
  advanced-search `toggle_favorite_results` free friend (Phase 68).
- `parent_group.*` — `ParentGroup{parent, names}` + `group_by_parent(paths)`
  (pure/tested): splits full slash-paths per parent gallery, order-preserving.
  Collection screens list hits from DIFFERENT parents but `transfer_images`
  works per source gallery — this is the bridge (Phase 68).
- `collection_ops.*` — `CollectionBatchOps` (Phase 68): the shared batch-op
  component for collection screens (FavoritesScreen base + AdvancedSearchScreen)
  — consent-gated export → App-owned FolderDialog pick → `FileOpJob::start_export`
  (the collect callback resolves selection nodes at pick-land time, never across
  the async pick), and grouped move/copy via `TransferDialog::open_collection`.
  Owns consent/job/transfer; enforces Phase 50 import-queue exclusivity
  (`set_exclusive` + release on completion/close) and the vault-hands-off
  contract: while `busy()` the host must not walk the tree or submit decodes
  (hosts draw chrome + `ops.render()` only). `poll()` returns
  `{status, reload, dirty}` drained every frame. **Phase 74:** `request_delete(paths,
  status&)` — prunes descendants, tallies via `summarize_batch_delete`, shows the
  shared default-cancel DANGER confirm (`draw_batch_delete_confirm`; non-empty
  `delete_paths_` == modal up, swallows every event), Y → queue exclusivity +
  `FileOpJob::start_delete_batch`; a Delete outcome sets `poll().reload` and releases
  exclusivity. The job-progress modal title is now kind-aware ("Deleting…"/"Exporting…").
- `batch_delete.*` — Phase 74 pure helpers shared by the grid and CollectionBatchOps so
  confirm-modal numbers cannot drift: `prune_descendant_paths` (drops paths inside another
  selected gallery path, component-boundary safe, order-preserving),
  `BatchDeleteSummary{top_level, galleries, images, videos, bytes, item_total}` +
  `summarize_batch_delete(vault, paths)` (resolves against the live index; galleries tally
  recursively via `count_subtree`; non-resolving paths skipped), `batch_delete_counts_line`
  ("2 galleries · 7 images · 312 MB", zero categories dropped), and the drawing-only
  `draw_batch_delete_confirm`. Tests: `tests/ui/test_batch_delete.cpp`.
- `position_label.*` — `position_label(index, count)` → `"3 / 128"` ("" when
  empty/out of range) — the ONE n/N formatter (Phase 68): grid chrome line
  (right-aligned in `FooterStatus`), favorites/tag title line, search-results
  header (`Results (N) · n / N`), viewer strip badge.

## Layout modules — pure geometry helpers (Phase 56 extractions)
- `text_metrics.{h,cpp}` — font-derived text pitch: `line_pitch(font_px)` → `ceil(font_px * 1.25)` and `row_height(font_px, pad)` → `line_pitch + 2 * pad`. The 1.25 leading guarantees each pitch exceeds the font height, so adjacent lines cannot touch and a whole-line clip band cannot cut a descender. Pure, unit-tested; the SINGLE source of truth for all text-line pitches.
- `detail_layout.{h,cpp}` — detail-panel line layout. Pure, unit-tested. Used by `detail_panel.*`.
- `prompt_layout.{h,cpp}` — centred prompt/summary box geometry (used by tag overview edit prompt and import-summary modal). Pure, unit-tested. Used by `tag_overview.*` and `import_status_screen.*`.
- `list_layout.{h,cpp}` — shared vertical-list geometry for five surfaces (advanced search, saved-search panel, search result view, search overlay, tag editor). Pure, unit-tested. Encapsulates all list row heights and scrolling maths for these screens.
- `import_status_row.{h,cpp}` — Import Status two-line row formatting: `format_task_route(task)` → `"name → dest"`, `format_task_status(task)` → state string, `import_row_height(font_px, pad)` → `2 * pitch + 2 * pad`. Pure, unit-tested.
- `album_rebind.{h,cpp}` — Viewer album binding logic: when the vault tree changes, remember the current item's path, re-list the album, then look up the remembered path. Returns whether the item survives and whether to preserve zoom/pan/playback state. Pure, unit-tested. Used by `ImageViewer::on_vault_changed`. Also owns the two index-space converters `media_index_in_listing` / `listing_index_of_media`: the grid and the search screens index the FULL sorted listing (sub-galleries partitioned first — Phase 46 mixed galleries), while the viewer's album is media-only, so `ImageViewer::on_enter` converts the incoming index and `go_back()` converts back for the `ToGallery` selection seed. Without the conversion every media item after the sub-gallery block opened shifted by the number of sub-galleries.

## Pure view / sort / model helpers (SDL-free unless noted, all unit-tested)
- `child_counts.*` (Phase 51) — `direct_child_counts(node) -> SubtreeCounts` (galleries, images, videos counted
  separately, reusing the existing SubtreeCounts struct); `format_tile_counts(counts) -> string` — plural-aware
  formatting ("3 galleries · 12 items" / "1 gallery · 1 item" / "12 items" / "empty"), collapsing images+videos
  to "items". Counts reserved per gallery listing (never per tile); cell does not grow; label moves up,
  thumbnail shrinks by row height, leaving grid metrics and hit-testing untouched.
- `gallery_view.h/.cpp` — `GalleryView{List,GridS,GridM,GridL,GridXL}` shared enum;
  `cell_size_for(view)` (S=192/M=256/L=352/XL=448 since Phase 75; List unused) +
  `next_gallery_view(view)` (the `L`-key cycle). GridM was 188 (the old fixed CELL) before the
  Phase 75 bump; stored thumbs are 512 px (`image::THUMB_MAX_SIDE`) so XL stays sharp. `gallery_view.cpp` is listed
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
  CoverCache and clears failed thumbs. **Phase 68:** owns a `SelectionModel` (Space toggles,
  Ctrl+A select-all-or-clear in handle_key; accessors `selection()`/`clear_selection()`;
  cleared by update_results — indices are per-listing). Accent badge on selected grid tiles;
  the list view's gutter marker + the `Results (N) · n / N` headers are drawn by the hosts.
  AdvancedSearchScreen's B/X/M over the selection are FREE FRIENDS (S1448) —
  `toggle_favorite_results` / `start_export_results` / `start_transfer_results(screen&)` —
  active only with Results focus, running through the screen's `CollectionBatchOps ops_`.
- `saved_search_panel.*` — saved-search sidebar: list rendering + CRUD (Ctrl+S/Enter/Del). Pure
  vault/SDL-free. Phase 54: `save_buf_` is a `TextInputModel` and `active_buffer()` returns
  `ITextInput*`; before that this field had NO Backspace handler at all, so a typo could only
  be undone by cancelling the save (regression-tested now). **Phase 56:** list layout derives from `list_layout.*` module.
  **Phase 68:** wheel-scrollable — `handle_wheel(wheel_y, max_h)` + a scroll offset clamped by
  `list_clamp_scroll` (pure, in `list_layout.*`); rows clip outside the viewport, the header
  stays fixed, and Up/Down keep the focused row visible (last max_h memoised from render,
  which now takes the window height). AdvancedSearchScreen routes wheel events over the
  sidebar region to it (after the detail-panel hit check).
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
  Also home of `tag_has_renderable_text(sv)` (PR #148): true iff the tag has ≥1 ASCII
  letter/digit — the font atlas is printable-ASCII-only, so a failing tag renders as an empty
  bracket shell (`[()]`, `[]`, `()`) or a blank chip. Every tag-import parse point drops
  failing tags (`parse_tag_list` lines, `meta_gallery_tags`); it is also the junk predicate
  for `Vault::prune_tags` (Ctrl+X cleanup, see tag_overview).
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
- `compact_album.*` (Phase 58) — `compact_album(vault, root_node_path) -> vector<IndexNode*>` —
  flattened list of media nodes for collection-mode viewers (favorites, tag overview, search
  results). Re-resolves `IndexNode*` pointers from paths to survive drains (import batches).
  Called on every viewer `on_vault_changed`, so collection viewers' album stays valid across
  vault mutations. Delegates to `vault::resolve_node` (path-safe). Fixes dangling pointers that
  caused playback/selection reset during imports.
- `strip_layout.*` — orientation-aware viewer-strip geometry + half-size thumbnails.
  Phase 47: `strip_cell_rect(...)` added for forward index→rect mapping (inverse `strip_hit_axis`
  pre-existed). NOTE: `gfx::Renderer::draw_thumbnail_strip` duplicates this layout internally
  (gfx must not depend on ui) — both sites carry SYNC comments; keep in sync on geometry changes.
  **Phase 68:** `strip_counter_rect(side, strip, text_w, line_h)` + `fullscreen_counter_rect(
  win_w, win_h, text_w, line_h)` — the n/N badge rects (strip far edge windowed; window
  bottom-right in fullscreen where strip+header are hidden). Pure/tested.
- `strip_scroll.*` (Phase 68) — pure manual strip-scroll state: `StripScrollState{offset,
  manual}` + `strip_apply_wheel` (clamps to `strip_content_extent`; no-op when content fits) +
  `strip_follow_index` (re-engages auto-centering). ImageViewer: wheel anywhere in the strip
  BAND (rect containment, NOT strip_hit — that returns -1 in gaps) scrolls the strip — checked
  before the video early-out so it works during playback; first wheel seeds offset from
  `strip_scroll_centered`; `show_image_at` re-engages follow. `render_strip` and `strip_hit`
  share ONE manual-aware scroll source (`current_strip_scroll`) so clicks stay accurate while
  scrolled.
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
  summing descendant `orig_size`. GalleryGrid Del with an EMPTY selection removes the focused
  image/video (`Vault::remove_image`) or gallery subtree (`Vault::remove_gallery`) behind the
  modal; with a multi-selection (Phase 74) it batch-deletes via `selected_delete_paths()`
  (live listing + `prune_descendant_paths`) → `FileOpJob::start_delete_batch` → ONE
  `vault::remove_nodes_batch` commit, behind the aggregate DANGER modal (summary snapshotted
  at Del time, paths rebuilt at confirm time; `naming_.batch_delete` cleared on every exit).
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
- `waste_threshold.h` — vault-bloat thresholds. Three predicates, and **which one gates what
  matters** (Phase 82 bug: the hint predicate was reused as the keypress gate, making compaction
  unreachable on exactly the vaults that needed it):
  - `should_display_waste(wasted, file_size)` — waste >= max(50 MiB, 10% of file). Gates the
    **passive footer hint only**. Never gate a user action with it.
  - `should_offer_compact(wasted)` — waste > 0. Gates GalleryGrid's **`Shift+C`** compact-confirm
    modal, which states the exact amount and defaults to cancel, so the user makes the I/O
    trade-off. Note `Vault::auto_reclaim_space` has its own, much stricter gate
    (`waste >= 256 KiB && waste * 4 >= size`), so between the two there is a wide band where
    nothing is reclaimed automatically and `Shift+C` is the only way to recover the space.
  - `should_hint_cancelled_import_waste(wasted)` — waste >= 1 MiB.
- `keybindings.h` — pure layout-independent key resolution: `bracket_key_for_scancode` maps the
  two physical keys right of `P` -> `BracketKey{Decrease,Increase}` by SDL SCANCODE (video
  volume `[`/`]` + slideshow dwell on any layout). Centralises the character-resolved
  `is_search_key`/`is_advanced_search_key`/`is_quick_switch_key` helpers.
  `is_unmodified(SDL_Keymod)` — the ONLY sanctioned "is this a bare key press" test: SDL3 keeps
  the LOCK modifiers (`SDL_KMOD_NUM`/`CAPS`/`SCROLL`) in the same `modstate` word it copies into
  every `SDL_KeyboardEvent::mod`, so `key.mod == 0` silently disables a bare shortcut for anyone
  with Num Lock on (Phase 78 bug: split-view `Tab`/`M` were dead). It masks the locks out and
  rejects only Shift/Ctrl/Alt/GUI/AltGr. Never write `key.mod == 0`.
- `dup_model.*` (Phase 61, extended Phase 62) — pure duplicate-finder model: `dhash64` (RGB buffer → 9×8
  grayscale cells → 64-bit difference hash), `hamming64`, `cluster_similar` (union-find at
  Hamming ≤ `DUP_SIMILAR_MAX_BITS` = 5); `DupGroup` (Identical / Similar-N-bits, sorted
  largest-reclaimable-bytes first) + `DupReview` KEEP/REMOVE marking state with the
  at-least-one-KEEP-per-group invariant. Phase 63: the ctor pre-marks every group
  keep-first/remove-rest (an untouched review applies to one copy per group) and
  `toggle` returns bool, refusing to unmark a group's last keeper — all-REMOVE is
  unreachable via the UI (`A` = keep only focused; the apply-time refusal stays as
  defense). Phase 62 adds the video-signature model: `VideoSig` (poster hash +
  5 sampled-frame hashes + validity bitmask), `duration_close` (±`DUP_VID_DURATION_TOL`=2 %
  with `DUP_VID_DURATION_ABS_US`=500 ms floor), `video_sig_match` (frame evidence decides
  when ≥ `DUP_VID_MIN_MATCHED`=4 of 5 positions are shared-valid at Hamming ≤
  `DUP_VID_FRAME_MAX_BITS`=7; otherwise poster fallback at ≤ 10 bits), and
  `cluster_video_sigs` (union-find over duration_close && video_sig_match pairs).
  `DupGroup::Kind` gains `SimilarVideo` (renders "Similar video"). Listed explicitly in
  osv_tests' premake5.lua files{}.
  **Phase 64 wave window:** `DUP_WAVE_GROUPS`=20; the CURRENT wave is always the
  FIRST `wave_size()` groups of `groups()`; `wave_index()`/`wave_count()`/
  `finish_wave()` (erases the front window, counts it done, resets touched).
  toggle/keep_only refuse `g >= wave_size()`; any_marked/can_apply/marked_count/
  marked_bytes/marked_paths iterate the wave only. `refresh_members(fn)`:
  fn(DupMember&)→false drops the member, groups shrinking < 2 are dropped,
  default marks re-applied — call only right after `finish_wave()`.
