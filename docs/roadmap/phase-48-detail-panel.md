## Phase 48 — Gallery detail panel ✅

**Goal:** A toggleable right-edge detail panel on the gallery-browsing screens
showing metadata for the focused node: name, type/codec, dimensions, size, date,
and own + inherited tags; a recursive tally and total size for galleries; and an
aggregate summary for multi-selections. Opens/closes via `D` (`Ctrl+D` on advanced
search); `Ctrl+Up`/`Ctrl+Down` and the mouse wheel over the panel scroll; and closes automatically below 640 px window
width to preserve browsing space.

### Completed work
- `detail_model.*` in `src/ui/` — pure SDL-/gfx-free content model. `DetailRow` /
  `DetailSection` / `DetailContent` types; `build_node_details(node, inherited)`
  for single nodes (images/videos show type/size/dimensions/date; galleries show
  recursive tally + total size; both show own + inherited tag sections);
  `build_selection_details(nodes, inherited)` for multi-selections (per-kind
  counts, summed size, case-insensitive intersection of selected items' own tags,
  or "no shared tags"). Delegates all string formatting to `meta_format.h`;
  gallery totals computed via `ui::count_subtree`. Unit-tested (21 tests).
- `detail_panel.*` in `src/ui/` — right-edge panel: `DetailPanelState{open,scroll}`
  to track open/closed and scroll offset; pure `detail_panel_width(open, window_w)`
  returns panel width (0 when closed OR window width < 640 px; otherwise 280 px);
  `draw_detail_panel(...)` renders the panel, returns content height for scroll
  clamping, culls rows to the window rect, and elides every vault string via
  `ui::fit_text`; `handle_detail_panel_scroll` handles `Ctrl+Up`/`Ctrl+Down`, and the pure
   `detail_panel_hit` / `scroll_detail_panel` pair backs mouse-wheel scrolling
  scrolling. Hosted by GalleryGrid, FavoritesScreen (covers all four subclasses:
  FavoritesImages, FavoritesGalleries, TagImages, TagGalleries), and
  AdvancedSearchScreen. Unit-tested (5 tests).
- `delete_summary.*` changed — `SubtreeCounts` gained `uint64_t bytes`; the
  `count_subtree` free friend now sums descendant `orig_size` values alongside
  the existing node counts.
- `selection_model.*` changed — gained `revision()`, a monotonic counter
  incremented on `toggle()` and `clear()`. Load-bearing for the detail panel's
  cache key: `GalleryGrid::refresh()` calls `sel_.clear()`, so the panel's
  cached content evicts when `children_` is repopulated, preventing stale
  `IndexNode*` reuse.
- `meta_format.*` changed — added `video_container_name(VideoCodec)` returning
  container label ("MP4"/"MKV"/"-" for unknown codecs).
- `gallery_session_state.h` changed — added `bool detail_open`, persisted
  across screen transitions within a session (App seeds and reads back on enter/exit).
- `gallery_grid.*` changed — panel field added to the class; `content_width(const
  GalleryGrid&)` free friend is now the SINGLE source of layout width (window
  minus panel). All four former `win_.width()` layout sites route through it:
  `on_enter()`'s `cols_` seed, `update_scroll_to_selection_grid()`, `render()`,
  and both uses in `hit_test()`. Using `win_.width()` directly would desync
  picking from drawing. `rebuild_detail()` is also a free friend (both needed
  `friend` declarations); one new data member bundled under cpp:S1820/S1448.
- `favorites_screen.*` changed — panel + detail cache state in the base class,
  covering all four favorite/tag subclasses. `reload()` clears the detail cache
  key because `SearchHit::node` pointers die on re-fetch. `current_detail_open()`
  free friend for App readback.
- `advanced_search_screen.*` changed — panel + detail cache state, with `Ctrl+D`
  toggles (bare `D` would type into the query buffer). All result repopulation
  funnels through `rerun()`, which clears the cache key.
- `app.cpp` changed — seeds and reads back `session_.detail_open` across screen
  transitions (gallery grid, all favorites/tag screens, advanced search).
- `premake5.lua` changed — `detail_model.cpp` and `detail_panel.cpp` added to
  the test-build file list (`osv_tests`).

Test count: 998 → 1040.

### Acceptance criterion
- The detail panel toggles on GalleryGrid and all four favorites/tag screens via `D`.
- The detail panel toggles on AdvancedSearchScreen via `Ctrl+D` (bare `D` types into the query).
- The detail panel scrolls within itself via `Ctrl+Up`/`Ctrl+Down`, and via the mouse wheel while the cursor is over the panel (the grid does not also scroll).
- Opening the detail panel reflows the grid into the reduced width rather than
  overlaying tiles; closing it restores full width.
- The detail panel stays hidden below a 640 px window width, preserving full grid
  browsing width on small screens.
- The focused node's details show name, type/codec, dimensions, size, date, own
  tags, and inherited tags (ancestor cascade); galleries additionally show a
  recursive tally (images/videos/galleries) and total encrypted size.
- A multi-item selection shows an aggregate summary: per-kind counts, summed
  encrypted size, and the case-insensitive intersection of selected items' own
  tags (or "no shared tags" if the intersection is empty).
- The open/closed state is session-global and persists across screen transitions.
- `scripts/test.sh` green (1040 tests).
- `scripts/test.sh --asan` clean (no memory/UB errors).

### Follow-ups
- **`SearchHit::node` pointer stability:** Both FavoritesScreen and
  AdvancedSearchScreen clear the detail cache key when results are re-fetched,
  since `SearchHit::node` pointers are valid only until the next mutating call.
  Any future search/favorites refactoring must maintain this cache-invalidation
  pattern to prevent use-after-free.
- **Layout width single-source rule:** `GalleryGrid::content_width` is now the
  only source of layout width for rendering, scroll-to-selection, grid column
  seed, and hit-testing. Future grid layout changes must update this one
  function, not scatter `win_.width()` calls.
- **Session-global detail state:** The detail panel's open/closed state lives in
  `GallerySessionState`, not on each screen. If a screen is added in the future
  that should host the detail panel, simply read and write
  `session_.detail_open` in the same pattern used by GalleryGrid, FavoritesScreen,
  and AdvancedSearchScreen.
