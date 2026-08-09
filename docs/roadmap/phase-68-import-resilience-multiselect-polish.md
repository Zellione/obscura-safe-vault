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

- [x] **Reproduce & root-cause** — corrupt fixtures added (garbage-with-magic
      zip, truncated zip, huge-declared-size entry). Two root causes found and
      fixed beyond the catch-all: **(a) a worker-result write-back defect** —
      `mark_task_complete` discarded the worker's local task copy entirely, so
      even a *gracefully* failed archive rendered as "✓ Done, 0 imported"
      (results now merge back; counts via `max()` against the live
      drain-incremented counters); **(b) a corrupt ROOT archive was folded into
      `skipped`** — `RecursiveTally` gained `root_unreadable`, and
      `import_archive_bytes_recursive` now fails the task with "Archive is
      corrupt or unreadable" instead of reporting a skip.
- [x] **Task-boundary catch-all** — worker dispatch and the decode-pool job body
      wrapped in try/catch: the task ends **Failed** ("Internal import error:
      <what>") on the Import Status screen; the queue continues with the next
      task. Plus an allocation guard: `zip_entry_size_plausible` refuses a zip
      entry whose declared size exceeds deflate's ~1032:1 bound before an
      mlock'd buffer is sized from the lie (a `0xFFFFFFFF` claim is a 4 GiB
      zero-initialised allocation the catch cannot see — OOM-kill class).
- [x] **Log to the error logfile** — every Failed import logs
      `platform::log_error("Import", import_failure_log_line(name, reason))` to
      `<config_dir>/error.log`, from one choke point after `mark_task_complete`.
      Names + ASCII reasons only (invariant #5).
- [x] **Tests** — corrupt/truncated/lying-header fixtures fail the task without
      terminating; a test-only worker hook (`test_only_set_task_hook`) proves an
      escaping exception fails THAT task and the next one still imports;
      `--asan` green.

### Part 2 — Multiselect for favorite / export / move / copy

The grid already runs export (`X`) and move/copy (`M`) off the selection, but
with gaps; the collection screens have no multiselect at all.

- [x] **Grid: `B` acts on the selection** — `toggle_favorite_selection` free
      friend: any-unfavorited → favorite all, else unfavorite all
      (`ui::batch_favorite_target`), persisted by a new vault free friend
      `vault::set_favorites_batch` — ONE `commit_index()` for the batch, none
      when nothing changed. Empty selection keeps the focused-tile toggle.
- [x] **Grid: move/copy accepts any mixed selection** — the `is_image()` filter
      that silently dropped **videos** is now `is_media()`, the
      only-one-kind refusal is gone, and a mixed selection routes to
      `TransferDialog::open_mixed` → `FileOpJob::start_transfer_mixed`
      (media then gallery subtrees, tallies merged into one outcome).
- [x] **Collection screens: full multiselect** — `FavoritesScreen` base (covers
      favorites/tag images+galleries) and `SearchResultView` gained
      `SelectionModel` semantics: Space toggles (Enter still opens), `Ctrl+A`
      select-all-or-clear, accent selection badge on tiles/rows, selection
      revision feeding the Phase 48 detail-panel aggregate.
- [x] **Collection screens: `B` / `X` / `M` over the selection** — a new shared
      `ui::CollectionBatchOps` component (consent-gated export → folder pick →
      `FileOpJob`, and per-parent grouped transfer via `ui::group_by_parent` +
      `TransferDialog::open_collection` / `FileOpJob::start_transfer_collection`)
      hosts the flows on both screen families, with the Phase 50 import-queue
      exclusivity gate and vault-hands-off rendering while a worker owns the
      handle. Unfavoriting reloads the collection (removed tiles vanish;
      selection cleared with the stale listing).
- [x] **Tests** — pure toggle rule, per-parent grouping, batch favorite
      persistence across reopen, mixed media+gallery transfer, video-included
      transfer, grouped cross-parent transfer.

### Part 3 — Wheel scrolling on side panels

- [x] **Viewer thumbnail strip** — a wheel with the cursor anywhere in the strip
      band scrolls the strip along its axis (both dock sides; works while a
      video plays) instead of zooming. New pure `ui::StripScrollState`
      (`strip_scroll.*`): manual offset seeded from the auto-centered position
      on first wheel, clamped to content, re-engaging auto-centering on image
      change; `render_strip` and `strip_hit` share one manual-aware scroll
      source so clicks stay accurate while scrolled.
- [x] **Saved-search sidebar** — `SavedSearchPanel` gained a wheel-driven scroll
      offset (clamped via a new pure `list_clamp_scroll` helper), row clipping,
      and keyboard-nav keep-visible; `advanced_search_screen` routes wheel
      events over the sidebar region to it.
- [x] **Detail-panel audit** — verified: all three `draw_detail_panel` hosts
      (grid, favorites base, advanced search) already route the wheel via
      `detail_panel_hit`; no other host exists. No code change needed.
- [x] **Tests** — pure strip-scroll state tests (clamp, content-fits no-op,
      manual flag, re-engage) + list-clamp tests.

### Part 4 — `n / N` position counters

- [x] **Viewer strip overlay** — an `n / N` badge (STRIP_BG fill, BORDER
      outline, TEXT_DIM) at the strip band's far edge for both dock sides
      (`strip_counter_rect`, pure/tested); in **fullscreen**, where strip and
      header are hidden, the same badge anchors to the window's bottom-right
      (`fullscreen_counter_rect`) so position stays visible — the point of the
      feature. Hidden when the album is empty.
- [x] **Grid footer counter** — the focused tile's `n / N` over the full sorted
      listing joins the existing waste/selection chrome line, right-aligned;
      the footer band's error > import summary > status priority is untouched.
- [x] **Collection screens** — right-aligned counter on the favorites/tag title
      line (all four subclasses share it); the search-results header reads
      `Results (N) · n / N` in both the list and grid views.
- [x] **Tests** — `position_label` formatting/bounds + badge-rect placement per
      dock side and fullscreen. `n` follows the same `children_` order the grid
      renders (Phase 46 partition included), so index and eye agree by
      construction.

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

**Delivered defect discoveries (pre-existing, fixed here):** the import worker's
result write-back discard (failed archives rendered as Done since Phase 50), the
corrupt-root-archive-as-skip fold (Phase 53), and the video-dropping
`is_image()` filter in selection transfer (Phase 44).

**Known limits:** collection screens report transfer failure *counts* in the
status line without the grid's per-item `FailureListDialog`; exporting a
selected gallery hit on the search screen is skipped (media only), matching
export's originals-only rule.

**Status:** ✅ Shipped — 1895 tests / 0 failed (+ ASAN). Merged in PR #173.
