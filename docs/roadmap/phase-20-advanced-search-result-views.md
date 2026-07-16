## Phase 20 — Advanced-search list/grid result views ✅

**Goal:** Let the Phase 18 advanced-search screen toggle its result panel between
the existing **list view** and a **thumbnail grid view**.

### Tasks
- [x] **Result view mode** — extend `AdvancedSearchScreen` with a session-scoped result-view state (`List` | `Grid`), defaulting to `List` (current behaviour). Toggle on a non-text key (`Ctrl+L`) so it never collides with typing into the query/group fields.
- [x] **Grid rendering** — render results as thumbnail tiles reusing the gallery grid's `draw_tile_thumb` (including the Phase 19 covers for gallery results and the video play-badge); the list view is unchanged. Result activation (Enter → viewer over the containing gallery for media, navigate for galleries) behaves identically in both modes.
- [x] **No vault/format change** — purely a presentation toggle over the existing `run_search` result set.
- [x] `tests/ui/` — view-mode toggle state machine (default, cycle, persistence within the screen's lifetime); navigation maps to the correct index in both list and grid modes.

### Acceptance criterion
On the advanced-search screen, `Ctrl+L` switches the live results between a list
and a thumbnail grid; selecting a result opens/navigates the same target in both
modes; the Phase 12 `/` overlay and the rest of Phase 18 are unchanged.

**Status:** ✅ 488 tests pass under `scripts/test.sh` and `--asan` (ASAN+UBSan+LSan
clean) — 7 new `result_grid` tests (toggle state machine + List/Grid move deltas +
clamped/empty navigation). The result-view state machine is the pure, SDL-free
`src/ui/result_grid.{h,cpp}` (`ResultView{List,Grid}` + `toggle_result_view` +
`result_move_delta`/`result_move`); the screen stores a session-scoped
`result_view_` (default List) toggled by `Ctrl+L`, and `handle_results_key` drives
the cursor through `result_move` (List: ±1 row; Grid: ±1 / ±cols). The Phase 19
tile-thumbnail draw (gallery covers + montage + video play-badge) was extracted
verbatim into a shared `src/ui/tile_thumb.{h,cpp}` (`ThumbContext` bundle +
`draw_tile_thumb`/`tile_thumb_texture`/`tile_cover_tex`), now reused by both
`GalleryGrid` (delegating) and the new `render_result_grid` free friend. The
screen gained a `TextureCache&` ctor arg + its own off-thread decode worker +
`update()` pump (mirroring the gallery), so grid thumbnails decrypt → off-thread
decode → GPU upload through the existing pipeline — no vault/format change, no new
disk path. The Phase 12 `/` overlay and the rest of Phase 18 are untouched.

**Follow-up — session-preserved search + clear:** the advanced-search state
(query, builder buffers, cursor, focus, view mode) now persists across visits
within one unlocked-vault session via a session-scoped `ui::AdvancedSearchState`
(`src/ui/advanced_search_state.h`) that `App` owns and the screen restores in
`on_enter` / saves in `on_exit`; results are re-derived (not stored, so
`SearchHit::node` pointers can't dangle). `App` resets it whenever the active
vault changes (lock / switch / idle auto-lock, via `promote_pending`). `Ctrl+R`
clears the search behind a `Y/N` confirmation modal (resets the query to its
default and re-runs).
