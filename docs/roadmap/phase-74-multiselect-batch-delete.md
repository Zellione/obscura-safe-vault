# Multi-select batch delete (Phase 74)

**Status:** ✅ shipped
**Date:** 2026-08-10

## Problem

Phase 68 made B / X / M act on the Space/`Ctrl+A` multi-selection, but Del kept its
pre-multiselect behavior: on the gallery grid it silently ignored an active selection and
offered to delete only the focused tile, and the collection screens (favorites/tag,
advanced-search results) had no delete at all. Selecting ten items and pressing Del
deleted one — the safe direction, but a convention break the F1 help ("Del — Delete",
listed beside the batch-aware keys) actively reinforced.

## What shipped

- **Del acts on the selection everywhere the Phase 68 convention applies** — gallery
  grid, all four favorites/tag screens (shared `FavoritesScreen` base), and
  advanced-search **Results focus** (the query-builder focuses keep their chip-removal
  Del). Focused tile when the selection is empty; the grid's single-item flow is
  byte-for-byte unchanged.
- **One index commit for a mixed selection.** New `vault::remove_nodes_batch` (free
  friend beside `remove_media_batch`, which stays media-only and untouched): each full
  slash-path may name a media node or a whole gallery subtree; missing paths and the
  root are counted in `RemoveBatchStats`, not errors; ONE `commit_index()` (none if
  nothing removed) + one `auto_reclaim_space()`. Runs on the FileOpJob worker via the
  new `start_delete_batch` (progress jumps done→total on the single atomic commit).
- **Aggregate default-cancel confirm modal.** "Delete N selected items?" + a counts
  line ("2 galleries · 7 images · 3 videos · 312 MB") + the irreversibility warning,
  DANGER-bordered, `[Esc/N] Cancel  [Y] Delete`. The summary is snapshotted at Del
  time; the path list is rebuilt at confirm time from the live selection (a background
  import drain can remap the listing while the modal is up).
- **Shared pure helpers (`ui/batch_delete.*`)** keep the numbers honest on every
  surface: `prune_descendant_paths` (a collection-screen selection can hold gallery
  `G` and `G/a.jpg`; the descendant is dropped, path-component-boundary safe),
  `summarize_batch_delete` (recursive gallery tally via `count_subtree`),
  `batch_delete_counts_line`, and the drawing-only `draw_batch_delete_confirm`.
- **`CollectionBatchOps::request_delete`** gives every collection screen the identical
  flow — Phase 50 import-queue exclusivity, confirm modal, worker job, reload on
  completion — with no per-screen drift. The job-progress modal title is now
  kind-aware ("Deleting…" vs "Exporting…").
- **Latent Phase 68 bug fixed:** `AdvancedSearchScreen::render` only called
  `ops_.render()` inside its `busy()` early-return, so CollectionBatchOps modals (the
  export consent, and now the delete confirm) never drew on that screen. `ops_.render()`
  now also runs unconditionally as the last draw call.

## Deliberately unchanged

- No undo/trash — delete remains irreversible, exactly as before.
- No delete from the image viewer or the duplicates screen (which has its own
  KEEP/REMOVE flow).
- `remove_media_batch` callers (duplicate finder, transfer) — contract untouched.
- No `.osv` format change; `INDEX_VERSION` stays 11.

## Files

- `src/vault/vault.h` / `vault.cpp` — `remove_nodes_batch` + `erase_any_child`
- `src/ui/batch_delete.h` / `.cpp` — pure helpers + shared confirm drawer (new)
- `src/ui/file_op_job.h` / `.cpp` — `start_delete_batch` / `run_delete_batch`
- `src/ui/gallery_grid.h` / `.cpp` — selection-aware Del + aggregate modal
- `src/ui/collection_ops.h` / `.cpp` — `request_delete` + kind-aware progress title
- `src/ui/favorites_screen.h` / `.cpp` — Del wiring + help
- `src/ui/advanced_search_screen.h` / `.cpp` — Results-focus Del + render fix + help
- Tests: `tests/vault/test_remove_batch.cpp`, `tests/ui/test_batch_delete.cpp`,
  `tests/ui/test_file_op_job.cpp`
