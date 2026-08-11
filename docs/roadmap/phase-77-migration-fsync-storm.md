# Vault-upgrade fsync storm & stale progress label (Phase 77)

**Status:** ✅ shipped
**Date:** 2026-08-11

## Problem

Owner report: the Phase 65/75 blocking vault-upgrade modal hung indefinitely
near the end of a run, with the progress counter maxed out (`N / N`) and the
title still reading "Preparing…".

Root-caused with `systematic-debugging` (see conversation for the full trace)
to two independent defects in the same code path, both introduced or exposed
by Phase 75's thumbnail-regen migration arm:

1. **O(items) fsyncs, not O(1).** Phase 75's 512px thumbnail bump makes
   `migrated_thumb_side` stale for every pre-existing vault, so
   `MigrationJob::collect()` enqueues an `ImageThumb`/`VideoPoster` item for
   **every** image/video that already has a thumbnail — effectively the whole
   library on first upgrade, not a small delta. `apply_image_thumb`,
   `apply_video_poster`, and `apply_video_probe`'s poster arm each called
   `ChunkStore::sync()` — a real `fsync` — once **per item**, on the single
   coordinator thread, with no batching. Every other bulk-write path in this
   codebase batches its durable commits (Phase 50 import, Phase 69 transfer
   batching, `compact()`'s own `BATCH_BYTES`); this one didn't. On a vault with
   thousands of images that's thousands of serialized fsyncs — easily minutes
   of wall time, and it compounds with the auto-compaction the regen orphans
   almost certainly trigger (every old thumbnail chunk becomes waste,
   trivially clearing the 256 KiB `AUTO_COMPACT_MIN_WASTE` floor), which
   rewrites the vault's media payload a second time right after.
2. **The `Done` phase fell through to the "Preparing…" label.**
   `draw_migration_progress`'s phase switch had cases for `Repairing` /
   `Committing` / `Compacting` only; `Done` (and `Idle`/`Scanning`) hit
   `default: "Preparing…"`. Combined with (1)'s long stall, the modal could
   sit showing "Preparing…" next to a maxed-out `total/total` count line —
   textually indistinguishable from a genuine hang even once the job had
   actually finished.

## What shipped

- **Batched durability.** `apply_video_probe`, `apply_image_thumb`, and
  `apply_video_poster` (`src/vault/vault.h`/`.cpp`) take a new `sync` parameter
  (default `true`, preserving existing behavior for any other caller).
  `MigrationJob`'s coordinator (`src/ui/migration_job.cpp`) passes `sync=false`
  at all four call sites. Correctness rests on one property, proven from the
  existing code rather than newly added: `commit_migration()`'s own
  `commit_index()` already calls `fileutil::sync()` on the same `FILE*`
  **after** every apply in a run — and `fsync` flushes every buffered write on
  a file descriptor, not just the most recent one — so the single sync at
  commit time already makes every deferred chunk append durable before the
  index that references it becomes the active slot. A cancelled run still
  gets this: `commit_migration()` runs unconditionally on cancel (per the
  existing "work applied so far is committed and durable" contract), so
  partial batches are never silently lost. Pinned by
  `migration_job_thumb_regen_batches_syncs_not_one_per_item` (40-image batch,
  `fileutil::sync_call_count()` stays far under `40`) plus three focused
  vault-level tests asserting `sync=false` skips the fsync while the default
  keeps the old immediate-durability behavior.
- **A pure, testable label function.** The phase-label switch moved out of
  `app.cpp`'s `draw_migration_progress` (SDL-coupled, untestable) into
  `ui::migration_progress_text(MigrationPhase, done, total) ->
  MigrationProgressText{title, count_line}` (`src/ui/migration_job.h`/`.cpp`).
  `Done` now reads "Finishing…" instead of falling into the `Idle`/`Scanning`
  default. `draw_migration_progress` is now a thin wrapper that calls it and
  hands the result to `draw_op_progress`.

## Tests

- `apply_image_thumb_sync_false_skips_fsync`, `apply_video_poster_sync_false_skips_fsync`,
  `apply_video_probe_sync_false_skips_poster_fsync` (vault) — `sync=false`
  performs the append with zero calls to `fileutil::sync()`; the default
  (no argument) still syncs exactly once, pinning the old behavior for any
  other caller.
- `migration_job_thumb_regen_batches_syncs_not_one_per_item` (ui) — a 40-image
  whole-vault thumb regen stays under 40 total syncs (old behavior needed at
  least one per item plus the final commit).
- `migration_progress_text_done_does_not_read_as_preparing`,
  `migration_progress_text_covers_every_phase`,
  `migration_progress_text_count_line_falls_back_when_total_zero` (ui) — pin
  every `MigrationPhase`'s label, with `Done` explicitly asserted not to equal
  "Preparing…".

2009 tests / 0 failed; ASAN clean.

## Deliberately unchanged

- `apply_video_probe`/`apply_image_thumb`/`apply_video_poster`'s default
  argument (`sync = true`) — any future caller outside the migration
  coordinator keeps today's immediate-fsync-per-call behavior unless it
  explicitly opts out.
- The crash-safety contract: cancel semantics, the "committed work is durable"
  guarantee, and `commit_migration()`'s own 3-sync slot swap are untouched —
  this is purely a batching optimization sitting behind an already-correct
  durability boundary.
- No `.osv` format change, no `INDEX_VERSION` bump.
