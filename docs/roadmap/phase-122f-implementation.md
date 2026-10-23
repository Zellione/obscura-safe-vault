# Phase 122F — Import frame budgets and asynchronous durable saves

Implementation based on `7e5ccd9`; intended for stacking by the Phase 122 parent.
This document records the delivered subset and remaining validation; it does not
mark the entire benchmark matrix in the Phase 122 plan complete.

## Changes

- Drain at most 32 staged records, checking a 4 ms elapsed budget between records.
  This is a soft budget: an individual attachment or subsequent screen refresh
  can exceed it. FIFO placement and parent-before-child ordering are preserved.
- Bound the producer-to-main-thread record queue to 256 records. Backpressure
  releases the queue mutex while waiting. Drain, cancellation, and shutdown wake
  producers, including gallery-only records from large directory trees.
- Run import-triggered v3 `CommitLane::enqueue_snapshot()` on a dedicated async
  task. The existing synchronous v3 commit interface remains unchanged for other
  callers. No SQL durability, object authentication, or secure allocator settings
  were weakened. The v3 session mutex already serializes `stage_object()` against
  `commit_metadata()`; the producer can continue decoding outside that mutex.
- While `ImportQueue::saving()` is true, the App paints its own save modal and
  suppresses screen rendering, help evaluation, update, input dispatch, navigation,
  auto-lock, tree refresh, and tag review. The main thread reacquires tree ownership
  only through `poll_save()`/`wait_for_save()`. Shutdown joins before screen/vault
  teardown; queue teardown also joins before attaching pending records.
- Keep the event loop ticking while imports are busy. Idle 250 ms polling otherwise
  imposes a throughput ceiling once each drain is limited to 32 records.
- Never snapshot or flush under the import queue mutex. Completed task rows no
  longer prevent the final batch save. Enqueue explicitly re-arms the save latch,
  including a one-file task that finishes before the next drain. Opening an idle
  vault does not trigger an unnecessary full database rewrite. `busy()` includes
  durability work, not just extraction and pending attachments.
- Display discovery, extraction/reading, and processing/publication in import
  status rows. Saving has a separate modal with an honest unknown-total message
  and no nonfunctional cancel hint. An animated ellipsis shows save activity. `OSV_PERF_LOG=1` adds static-label timings for
  image/video processing, save, and drain (no filenames or decrypted content).

The archive/folder decoder remains sequential. This change does not introduce a
second solid-archive extraction cursor, additional plaintext copies, or parallel
SQL/tree mutation. The existing independent file-import decode pool is retained.

## Reproducible production-path measurement

The opt-in `import_bench_v3_folder_zip_solid` test exercises the real ImportQueue,
v3 object publication, thumbnail generation, attachment, and SQLCipher saves.
It uses 128 deterministic 64×64 PNGs and a destination containing 5,000 existing
galleries. Modes 0/1/2 are folder, ZIP, and libarchive-generated solid 7z (decoded
by the production 7-Zip reader). Every run checks the imported count. Total time
includes the final shutdown flush; the maximum drain measures the UI call only.
Fixture generation, destination creation, and prepopulation are outside timing.

```sh
OSV_BENCH_122F=1 build/bin/Debug/osv_tests import_bench_v3
# Choose a writable real-disk scratch directory for a storage measurement:
OSV_BENCH_122F=1 OSV_BENCH_DIR=/path/to/scratch build/bin/Debug/osv_tests import_bench_v3
```

Observed on Arch Linux, Ryzen 7 8845HS (8 cores/16 threads), Debug GCC build,
`/tmp` tmpfs. Three runs per version; median total time and worst drain across
those three runs. Baseline uses the exact ImportQueue header/implementation from
`7e5ccd9`, the same benchmark and production dependencies. The benchmark's common
progress structs carry the additive phase field in both binaries; no baseline
queue algorithm was substituted or simulated.

| Input | Before total | After total | Before files/s | After files/s | Before worst drain | After worst drain |
|---|---:|---:|---:|---:|---:|---:|
| Folder | 2.414 s | 2.154 s | 53.0 | 59.4 | 474.758 ms | 0.934 ms |
| ZIP | 2.531 s | 2.255 s | 50.6 | 56.8 | 508.809 ms | 0.706 ms |
| Solid 7z | 2.744 s | 1.915 s | 46.6 | 66.8 | 558.257 ms | 0.703 ms |

This isolates the demonstrated main-thread database-save stall: worst drain
falls below 1 ms on this corpus. Total wall time is noisier (the solid-7z after
runs ranged 1.815–2.624 s); concurrent work on this machine and tmpfs mean these
numbers are illustrative, not a promise of disk throughput. Both versions retain
the usual best-effort mlock behavior; the solid-archive path reported the existing
locked-memory warning in both versions. No memory-limit workaround was applied.

## Validation and integration notes

Integrated stack: full Debug suite passes **2,514 tests**. Full sanitizer and CI
gates are pending. The App integration retains both maintenance and import-save
ownership guards and shutdown joins.

- Focused Debug runner: **42 tests pass**, covering import queue integration,
  progress formatting, small-batch cold reopen without a shutdown save, bounded
  drains, cancellation waking a backpressured producer, and existing ZIP/folder/
  archive/exception/metadata-review cases. Benchmark: three successful runs per
  version, all three formats each time.
- `app.cpp` passes the normal build's `-Werror` syntax check. App/import queue/
  status/benchmark translation units also pass no-AV `-Werror` syntax checks
  (not a complete no-AV link or test run).
- A full `scripts/test.sh` attempt stopped on the shared tmpfs disk quota. At the
  parent's request, no further full/dependency builds were run. The isolated
  worktree's private dependency copies were removed and replaced by shared
  read-only-use symlinks; a focused runner reuses only local object files and
  rebuilds affected production units plus the relevant tests. Full Debug, ASAN,
  Release, no-AV, CI, and Sonar gates belong to the parent's integrated stack.
- App merge points with 122D: combine the `apply_nav()` save gate with the
  maintenance gate; keep maintenance's progress/return-section fields and
  `open_settings_overlay(bool)` behavior; combine run-loop predicates as
  `maintenance_ui_.running || import_ui_.queue.busy() || screen animation`;
  preserve both shutdown joins. Keep D's drain and help-evaluation guards. F's
  save branch renders before the ordinary screen/overlay/help branch, so it
  does not evaluate screen help at all while the save owns the tree.

## Remaining scope / limitations

The 4 ms drain limit does not bound one pathological attachment, screen refresh,
folder discovery, decoder cancellation latency, or shutdown's final join. Save
completion is durable but non-cancellable once SQL synchronization starts;
input/navigation waits while the modal continues repainting. The database still
rebuilds its metadata snapshot every commit. Incremental SQL updates could reduce
total save time further, but require a separate transactional design and tests.

Real-disk cold/warm tests, mixed large images/video, encrypted RAR/7z, TAR,
multipart/nested throughput comparisons, per-core CPU/I/O-wait counters,
secure-memory high-water instrumentation, and manual frame/input latency on the
integrated application are not measured here. Existing functional tests remain
necessary; the small PNG benchmark is not evidence for those workloads. Archive
opening and extraction currently share a coarse "Extracting / reading" phase;
there is no fabricated extraction-byte percentage or SQL percentage.
