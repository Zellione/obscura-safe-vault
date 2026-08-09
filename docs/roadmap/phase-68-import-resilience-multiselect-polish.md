# Phase 68 — Import-error resilience, multiselect everywhere & browsing polish 🔜

**Goal:** Four owner-reported gaps. A corrupt archive must never take the app
down — the import task fails visibly and the cause lands in the error logfile.
Multi-select (Space / `Ctrl+A`) drives favorite toggle, export, and move/copy on
the gallery grid **and** on the collection screens (favorites, tag, search
results). The mouse wheel scrolls the viewer thumbnail strip and the
saved-search sidebar under the cursor. Browse position is always visible as a
`n / N` counter — on the grid footer and as a strip overlay in the viewer.

### Part 1 — Corrupt-archive import: catch, fail, log (the bug)

Importing a corrupt archive closed the app with no message. The import queue's
worker (`ImportQueue` jthread + `DecodePool` workers, `src/ui/import_queue.cpp`)
runs archive parsing with **no try/catch at the task boundary** — any exception
escaping a worker thread is `std::terminate`. (`platform/error_log.cpp` installs
a terminate handler, so `<config_dir>/error.log` may already show a `[Fatal]`
line for the owner's crash — check it while reproducing.)

- [ ] **Reproduce & root-cause** — build corrupt fixtures (truncated, bit-flipped
      header, garbage central directory) for each import path: `.zip`/`.cbz`
      (miniz), `.7z`/`.rar`/`.tar` (libarchive), recursive nested archives, and
      multipart sets. Identify the actual throwing/faulting site and harden it
      (bounds/validation), not just the symptom.
- [ ] **Task-boundary catch-all** — wrap per-task execution on the import worker
      (and the decode-pool job body) in try/catch: the task ends **Failed** with
      an ASCII reason shown on the Import Status screen (`Shift+I`) and the
      footer summary; the queue moves on to the next task; the vault stays
      consistent (staged chunks for the failed task are orphaned dead ciphertext,
      reclaimable by compact — same guarantee as a crash mid-batch, Phase 50).
- [ ] **Log to the error logfile** — the failure is recorded via
      `platform::log_error("Import", …)` with the archive's filename and the
      failure reason. Never log archive *content*, passwords, or key material
      (invariant #5).
- [ ] **Tests** — each corrupt fixture imports as a Failed task without
      terminating the process; the error log contains one `[Import]` line per
      failure; a healthy archive queued after a corrupt one still imports.

### Part 2 — Multiselect for favorite / export / move / copy

The grid already runs export (`X`) and move/copy (`M`) off the selection, but
with gaps; the collection screens have no multiselect at all.

- [ ] **Grid: `B` acts on the selection** — with a non-empty selection, toggle
      favorite on every selected node (rule: if any selected node is not
      favorited → favorite all, else unfavorite all); empty selection keeps
      today's focused-tile behavior (`toggle_favorite_current`). One index
      commit for the batch, not one per node.
- [ ] **Grid: move/copy accepts any mixed selection** — `start_transfer_selection`
      (`src/ui/gallery_grid.cpp`) refuses images+galleries mixed and silently
      drops selected **videos** (`is_image()` filter — should be `is_media()`).
      A selection of any mix of images, videos, and galleries transfers in one
      dialog run; `TransferDialog` gains a mixed source (media names + gallery
      paths) or sequences the two existing sources behind one Mode/Dest pick.
- [ ] **Collection screens: full multiselect** — `FavoritesScreen` base (covers
      favorites images/galleries + tag images/galleries) and the advanced-search
      result panel gain the grid's `SelectionModel` semantics: Space toggles,
      `Ctrl+A` select-all (`ui::selectable` stays the one rule for what may
      enter a selection), selection badge on tiles, revision feeding the detail
      panel's existing multi-selection aggregate (Phase 48).
- [ ] **Collection screens: `B` / `X` / `M` over the selection** — these screens
      list nodes from *different parent galleries*, so export and transfer must
      carry a per-item parent path instead of the grid's single
      `(path, names)` pair — group by parent internally. Favorite-toggle rule
      identical to the grid. On the favorites screens, unfavoriting removes the
      tile — remap selection the way `ListingRemap` preserves it on the grid.
- [ ] **Tests** — pure-model tests for the toggle rule + per-parent grouping;
      screen tests: mixed selection transfers whole, videos included, `Ctrl+A`+`B`
      on a favorites screen empties it in one commit.

### Part 3 — Wheel scrolling on side panels

- [ ] **Viewer thumbnail strip** — a wheel event with the cursor inside
      `strip_rect()` scrolls the strip along its axis (any dock side; honour
      natural direction) instead of zooming the image (`ImageViewer::handle_wheel`
      currently always zooms/scrolls the media). Manual strip scroll suspends
      the auto-follow the same way existing strip dragging does, re-engaging on
      navigation.
- [ ] **Saved-search sidebar** — the advanced-search saved-searches panel
      (`saved_search_panel.*`) scrolls with the wheel when the cursor is over
      it; scrolling clamps to content and never moves the loaded-query state.
- [ ] **Detail-panel audit** — wheel routing exists on grid / favorites /
      advanced search (`detail_panel_hit` → `scroll_detail_panel`); audit every
      other host (tag overview, duplicates review, …) and add the same routing
      where the panel renders but the wheel falls through to the content below.
- [ ] **Tests** — pure hit/scroll-model tests (strip axis mapping, sidebar
      clamp); a wheel over the strip must not change zoom.

### Part 4 — `n / N` position counters

- [ ] **Viewer strip overlay** — a small `n / N` badge rendered at the strip's
      edge (all three dock sides), so position stays visible in **fullscreen**,
      where the header band (which already shows `name  n/N  zoom`) is hidden.
      Same STRIP_BG treatment so it reads as part of the strip; hidden while
      the strip is hidden.
- [ ] **Grid footer counter** — the gallery grid's footer band shows the focused
      tile's position as `n / N` over the full listing (sub-galleries + media,
      matching the visual order). Footer priority stays error > import summary >
      status; the counter joins the status segment, never displacing errors.
- [ ] **Collection screens** — the same focused-position counter on the
      favorites / tag / search-result grids (their headers or footers, matching
      each screen's existing chrome).
- [ ] **Tests** — counter string formatting + full-listing indexing (the
      Phase 46 mixed-gallery partition must not desync `n` from what the eye
      counts); fullscreen shows the strip badge iff the strip is shown.

**Out of scope (YAGNI):** F2 settings-overlay wheel support (keyboard-only stays);
multiselect on the duplicates-review screen (it has its own KEEP/REMOVE marking
model); drag-to-rubber-band selection; counters on dialogs/pickers.

### Acceptance criterion

A corrupted `.zip` and a corrupted `.7z` import as **Failed** tasks — the app
stays up, `Shift+I` shows the reason, and `error.log` records one `[Import]`
line each. On the grid and on a favorites screen, a Space/`Ctrl+A` selection
spanning images + videos (+ galleries on the grid) favorites, exports, and
moves/copies as one batch. The wheel scrolls the viewer strip and the
saved-search sidebar under the cursor without zooming or losing state. The grid
footer and the fullscreen viewer both show the current `n / N` position.

**Status:** 🔜 Planned.
