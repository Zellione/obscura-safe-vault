# Grid XXL density + shared density everywhere (Phase 93)

**Status:** ✅ shipped
**Date:** 2026-08-28

## Problem

Two gaps:

1. **No very-large grid.** The largest gallery density (`GalleryView::GridXL`,
   448 px since Phase 75) was still smaller than the sharp stored-thumbnail
   budget (`image::THUMB_MAX_SIDE = 512`), and the densities had not been
   touched since Phase 75's bump. A bigger **Grid XXL** that stays within the
   512 px budget gives a genuinely large catalog view with zero migration cost.
2. **The density lived only on the gallery grid.** Favorites, the tag screens,
   and advanced-search results rendered fixed tile sizes (`CELL = 188`,
   `TILE = 92`) with no L-key cycle at all — while `GalleryGrid` had the full
   `List → GridS/M/L/XL` density enum persisted machine-wide
   (`gallery_view.conf`). The UI was inconsistent: one density concept in the
   tree browser, none in its collections.

## What shipped

### (a) Six catalog sizes (no `.osv` change)

`GalleryView` gains **`GridXXL`**; every grid density steps up by `+32`:

| Mode | Side (px) |
|---|---|
| GridS | 224 (was 192) |
| GridM | 288 (was 256) |
| GridL | 384 (was 352) |
| GridXL | 480 (was 448) |
| **GridXXL** | **512 (new)** |

XXL is exactly `THUMB_MAX_SIDE`, so existing stored thumbnails/posters render
sharp with **no migration** and no `INDEX_VERSION` bump. New pure
`next_grid_density` (grid-density-only cycle for the list-less collection
screens) and `grid_cell_size` (renders `List` as `GridM`) in
`src/ui/gallery_view.{h,cpp}`; the `{view,label,slug}` table gains the
`Grid XXL` / `grid-xxl` row.

### (b) One shared machine-wide density

New `ui::gallery_view_setting` process-global
(`src/ui/gallery_view_setting.{h,cpp}` — the `media::autoplay_setting`
pattern): the single in-memory source of truth, seeded from
`platform::GalleryViewPref` at `App::init` and `promote_pending`, written back
(and persisted to `gallery_view.conf`) by every surface's `L` key and the F2
"Default Gallery View" row. Because only one screen is active at a time, every
surface simply **reads the setting on entry and writes it on `L`** — changing
the density anywhere changes it everywhere (and the F2 row).

The now-redundant per-session `.view` fields are retired:
`GallerySessionState::view`, `DualSessionState::PaneState::view`, and
`GridLocation::view`. `GalleryGrid` reads the setting at construction; its `L`
handler cycles *from the setting* (self-healing if another surface changed it)
and writes it back; `set_gallery_view` (F2 live-push into an open grid) keeps
the setting in sync too.

### (c) The collection grids behave like the gallery grid

- **Favorites + tag screens** (`FavoritesScreen` base — favorites
  images/galleries, tag galleries/images): the fixed `CELL = 188` tile grid is
  now density-driven (`grid_cell_size`). `L` cycles `S→M→L→XL→XXL`
  (`next_grid_density`; `List` — which these grid-only screens can't show — is
  skipped and rendered as `GridM`), live-saves the shared setting, and shows
  the "View: <label>" status like the gallery grid. The `grid_spec` helper
  gains a `cell` parameter; every layout/scroll/hit-test/elide/badge site reads
  it. F1 gains an `L — Cycle grid size` entry.
- **Advanced-search results**: `ResultView{List,Grid}` is replaced by
  `GalleryView`. Plain **`L`** on Results focus cycles the full sequence
  `List → S → M → L → XL → XXL` and live-saves; the Phase 20 `Ctrl+L`
  List↔Grid toggle is superseded. `result_move_delta` treats any grid density
  as a row-stride grid (`±cols` vertically, `±1` horizontally) and `List` as
  one-per-row; the grid tiles size via `cell_size_for(grid_view_)`. The view is
  no longer stored in `AdvancedSearchState` — it is the shared setting.

## Tests

- `tests/ui/test_gallery_view.cpp` (8) — five distinct sizes with
  `S<M<L<XL<XXL` ordering; exact Phase 93 values; full L-cycle incl.
  `GridXL→GridXXL→List`; `next_grid_density` skips List and wraps S→S;
  `grid_cell_size(List) == cell_size_for(GridM)`; labels/slugs incl.
  `Grid XXL`/`grid-xxl`; slug round-trip for every value; `prev` inverts
  `next`.
- `tests/ui/test_result_grid.cpp` (5) — List move deltas (row ±1, no
  Left/Right); every grid density uses the same row-stride deltas; cols clamped
  to ≥1; `result_move` clamps into range; empty set yields 0.
- `tests/ui/test_gallery_view_setting.cpp` (2, new) — slot defaults to GridM
  before seed; set/get round-trips.
- `tests/platform/test_gallery_view_pref.cpp` — `grid-xxl` round-trips and the
  persisted file is exactly that slug + newline.
- `tests/ui/test_gallery_session_state.cpp`, `test_dual_session_state.cpp` —
  updated for the retired `.view` fields.

2197 tests / 0 failed (baseline 2191 + 6); ASAN clean.

## Deliberately unchanged

- **No `.osv` change, `INDEX_VERSION` stays 12** — sizes stay ≤ 512 px, so no
  thumbnail/posters regeneration migration (Phase 75's mechanism is untouched).
- **`gallery_view.conf` slugs are stable** — `grid-xxl` is additive; existing
  persisted values keep their meaning.
- **Favorites/tag screens have no List mode.** They are tile collections by
  design; "normal grid behaviour" here means the density cycle, not a metadata
  list (that would need a list renderer + list navigation).
- **The advanced-search List view is unchanged** — only the grid tiles grew a
  shared density; List remains the list it was, one result per row.