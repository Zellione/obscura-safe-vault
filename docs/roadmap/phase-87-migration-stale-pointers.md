# Migration leaves the grid holding freed node pointers (Phase 87)

**Status:** 🔜 ready for review
**Date:** 2026-08-25

## Problem

Crash of the 2nd vault (2026-08-25, `osv` pid 223157, `SIGSEGV`), root-caused
from the on-disk core dump (`coredumpctl`, 621 MB):

```
byte_at (src/gfx/text.cpp)
  ← next_codepoint
  ← FontAtlas::measure
  ← GalleryGrid::fit_name (src/ui/gallery_grid.cpp:2386)
  ← elide_middle (src/ui/widgets.h:110)
  ← render_grid_tile (src/ui/gallery_grid.cpp:2199)
  ← GalleryGrid::render
  ← App::render_frame (src/app/app.cpp)
```

The faulting node was `children_[0]` — a sub-gallery whose `name`
`std::string` was already freed: `_M_p == 0x0`, `_M_length == 17`,
`_M_capacity == 30`. Its child image node was intact (3955×2225 JPEG), so the
*node* had been relocated, not the data. At crash time the core showed the
migration job had **just finished** (`result_open == true`, `thumbs_fixed ==
205`, `reclaimed_bytes == 3662185`), `screen_` was the `GalleryGrid`, and all
17 worker threads were parked in `futex_wait` — a stale-pointer UAF, not a live
race.

Root cause: **a finished `MigrationJob` leaves the active screen holding
dangling `const IndexNode*`.** While the job is active its coordinator owns the
vault *exclusively* and mutates the index tree directly — thumbnail/poster
regen, then compaction. `Vault::compact()` rebuilds the tree into a copy and
publishes it with `root_ = std::move(new_root)` (src/vault/vault.cpp), which
**destroys the tree the grid was listing**; every cached pointer
(`GalleryGrid::children_`) dangles the instant `take_outcome()` returns. The
App collected the outcome in `App::update` but never told the active screen to
re-list, so the very next render read a freed node's name and crashed. This is
the exact invariant `Screen::on_vault_changed()` exists for (the Phase 50
import drain already honoured it; the migration completion path did not).

A second, same-class hazard: during the migration the grid was still rendering
— reading `children_[i]` while the coordinator (a worker thread) owned the
vault exclusively. The `migration_job.h` contract says the owning screen must
not read the vault until `take_outcome()` returns; `App::render_frame`/
`App::update` did not honour it. In this core the grid survived the in-flight
renders (the workers were parked at crash), but `compact()` freeing the tree
mid-render is the same danger.

## What shipped

- **Post-completion refresh (the confirmed crash).** New pure, unit-tested
  `app::apply_migration_refresh(has_active_vault, screen)`
  (src/app/migration_refresh.h) — `on_vault_changed()` + `mark_dirty()`, the
  same refresh the import drain does. `App::update` calls it immediately after
  `take_outcome()`, so the grid re-fetches fresh pointers before the next frame.
- **Exclusivity honoured (the latent race).** While a `MigrationJob` is active
  (`migration_ui_.job && migration_ui_.job->active()`), `App` pauses the
  screen's `update()` and skips its `render()`. The coordinator may be
  mid-compact, having freed the tree the grid listed; the migration modal is
  drawn directly by the App, so nothing is lost.

## Tests

- `migration_refresh_relists_and_requests_redraw`,
  `migration_refresh_is_a_noop_without_an_active_vault`,
  `migration_refresh_is_a_noop_without_a_screen`
  (tests/app/test_migration_refresh.cpp) — the refresh notifies the screen to
  re-list **and** requests a redraw, and is a clean no-op when there is no
  active vault or no screen. Verified with a `ui::Screen` double that records
  both effects.

2131 tests / 0 failed; ASAN clean. TSan: no new races — the 5 `radeonsi_drv_video.so`
data races from the Phase 42 known issue (local Mesa VA-API driver; CI's runner
has none) are byte-identical with and without this change.

## Deliberately unchanged

- `Vault::compact()`'s copy-rebuild-publish strategy — it is crash-safe by
  design (moves land in dead space; the publish is a single pointer swap).
- The `MigrationJob` cancel/commit contract and the `.osv` format /
  `INDEX_VERSION`.
- The Phase 42 local-only TSan issue (unrelated video-decode hwaccel path).
