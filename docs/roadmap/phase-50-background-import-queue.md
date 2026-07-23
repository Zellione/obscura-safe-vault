# Phase 50 — Background Import Queue: Design

**Date:** 2026-07-23
**Status:** Approved by owner (interactive brainstorming session)

## Goal

Move all three import flows — picked image/video files (`FileOpJob::start_import`),
zip/cbz via miniz (`ZipImportJob`), and 7z/rar/tar via libarchive — onto a single
persistent, queueable background pipeline. The vault stays **fully browsable while
imports run**: navigate galleries, view images, play videos. Add an Import Status
screen (per-item cancel + reorder) and a live footer-bar summary. Two performance
levers ship in the same phase: batched index commits and a parallel
thumbnail-decode pool. All six project-wide security invariants are preserved.

## Decisions made with the owner

| Question | Decision |
|---|---|
| Concurrency scope | **Full browsing** during import (view, navigate, play video) |
| Lock policy | **Suppress idle auto-lock while queue non-empty + confirm dialog** on manual lock/switch/quit |
| Performance | **Both** levers: batched index commits *and* parallel thumbnail decode |
| Status UX | **First-class Screen** with cancel + reorder, plus footer-bar summary |
| Architecture | **Main-thread tree**: index tree stays main-thread-only; worker stages chunks |

## Architecture — "main-thread tree"

The core rule: **the index tree is only ever touched by the main thread.** No tree
locking exists anywhere. The vault file gets two handles and one write mutex.

### File handles

- **Read handle:** `Vault` opens a second, read-only `FILE*` at unlock (closed +
  state wiped at lock). All read paths (thumbnail decrypt, full-image read, video
  demux via the `AVIOContext` bridge) move to it. Chunks are immutable once
  appended, so main-thread reads can never race worker appends — and since all
  reads remain main-thread-only (as today), the read handle needs no lock.
- **Write lane:** one mutex guarding the existing write `FILE*`. The import worker
  appends chunk data in **bounded slices** (~8 MiB per lock hold) so a main-thread
  commit never waits long behind a multi-GB video append.

### Import worker pipeline

One `std::thread` owned by an App-level `ImportQueue` (survives screen
transitions). Per file:

1. Read source bytes (file on disk, or archive entry via miniz/libarchive) into
   mlock'd `SecureBytes`.
2. Decode + thumbnail/poster generation — dispatched to a small **decode pool**
   (`min(hardware_concurrency, 4)` threads, mlock'd buffers).
3. Encrypt with a fresh 24-byte CSPRNG nonce per chunk; append chunks under the
   write lane (bounded slices).
4. Post a `StagedRecord{dest gallery path, ready IndexNode with chunk spans}` to a
   results channel.

**Ordering:** decode is parallel, but append + attach happen strictly in task
sequence order via a resequencing buffer — CBZ page order and insertion order are
preserved exactly.

### Main-thread drain

`App::update()` drains staged records each frame: re-checks name collisions
(skip + tally — enqueue-time checks can go stale), attaches nodes via
`Vault::attach_staged` (tree insert, **no commit**), and triggers `refresh()` on
the active screen so new items appear live while browsing.

### Batched, coalescing commit lane

- The main thread serializes the index to a blob (memory-only, fast) every
  **N = 32 attached files or 2 s**, whichever first, tagged with a monotonic
  generation, and hands it to the worker's **commit lane**.
- The lane writes the inactive index slot + fsyncs + flips `active_slot` in
  generation order, **coalescing**: only the newest pending blob is written. Each
  blob is a full snapshot, so skipping stale ones is always safe.
- Queue idle → user-op commits stay synchronous exactly as today. Queue active →
  user-op commits (tags, favorites, rename, sort, settings) route through the same
  ordered lane, so an older blob can never overwrite a newer tree.
- A commit-lane write failure is the one hard stop: queue halts, error surfaces on
  the status page + footer; already-committed work is safe.
- A final flush of the lane runs on queue drain, cancel, lock, and shutdown.

### Mutual exclusion with exclusive ops

The legacy "worker owns the vault" jobs cannot coexist with concurrent browsing:

- While the import queue is non-empty, **delete / transfer / combine / compact /
  password change are refused** with a footer hint ("Imports running — Shift+I").
- **Export stays allowed** (read-only over the read handle).
- Enqueueing is always allowed; the queue waits until any active exclusive job
  finishes before starting its next task.
- `ZipImportJob` and `FileOpJob::start_import` are **retired**; the executors
  (`zip_import.*`, `archive_import.*`, planners, meta.json handling) are reused by
  the new worker unchanged in spirit.

## Enqueue flow & queue semantics

All prompts resolve at **enqueue time**: the existing file dialogs, gallery-name
popup (meta.json prefill unchanged), and encrypted-archive password prompt run
first; the resulting fully-specified task is appended FIFO:

```
ImportTask{ id, kind (Files|Zip|Cbz|Archive|ArchiveCbz),
            source paths, dest gallery path, gallery name,
            optional archive password (crypto::SecureBytes) }
```

A wrong archive password discovered mid-import fails that task with a clear
outcome on the status page — no mid-queue re-prompt. Multiple imports can be
queued back-to-back from any gallery.

## UI surfaces

### Import Status screen

`NavKind::ToImportStatus`, opened via **Shift+I** globally or by clicking the
footer status. Shows:

- Running item: name, progress bar (done/total), source kind.
- Queued items: **Ctrl+Up/Down** reorder, **Del** cancel.
- Finished/failed items with outcomes (imported/skipped counts, error text).
- **C** clears finished entries. F1 help group. Esc returns to previous screen.

### Footer bar

The existing footer band (gallery grid `FOOTER_H` band + viewer chrome footer)
shows `Importing <name> 128/450 · 2 queued` while the queue is non-empty. Import
status takes priority over other transient status text.

## Lock policy

- Idle auto-lock **suppressed while the queue is non-empty** — App-level (the
  queue outlives screens), extending the existing `blocks_idle_lock` concept.
- Manual lock, vault switch, or quit with pending work → default-cancel confirm:
  *"N imports pending — finish current file, discard the rest, and lock?"*
  On confirm: current file completes, queue discarded, final commit-lane flush,
  passwords/keys wiped.

## Security review

- **Invariants 1–6 hold.** Decode/encrypt buffers are mlock'd `SecureBytes` wiped
  after use; fresh nonce per chunk; tag verified before decrypt on all read paths;
  no plaintext to disk anywhere in the pipeline; no key or content material logged
  from the worker.
- **New surface:** queued tasks hold source *paths* (harmless) and, for encrypted
  archives, a password in mlock'd `SecureBytes`, wiped on task completion, cancel,
  or lock. Same exposure class as today's in-flight password, potentially
  longer-lived; bounded by the confirm-on-lock rule.
- **Batched commits weaken durability, never integrity:** a crash loses at most
  the last batch's index entries; orphaned chunks are dead ciphertext reclaimable
  by the existing `Shift+C` compact. The 3-phase slot swap is unchanged.
- **Suppressed auto-lock** during long unattended queues is a deliberate
  availability tradeoff, mitigated by the explicit confirm on manual lock.

## Error handling

- Per-file soft failures (undecodable, name collision) skip + tally (as today).
- Task-level failures (bad archive, wrong password, I/O error) mark the task
  failed with an outcome; the queue continues.
- Commit-lane failure: hard stop (see above).

## Performance expectations

- Per-file `commit_index()` (full index serialization + 3 fsyncs **per file**) is
  replaced by the batched lane → ~32× fewer fsync rounds on many-file imports.
- Thumbnail decode (the per-file CPU cost) parallelized across the pool.
- Combined expectation: 5–20× faster on many-small-file imports; bounded-slice
  appends keep main-thread commit waits to low milliseconds.

## Testing

- **Pure, SDL-free models** (unit-tested): queue model (FIFO / reorder / cancel /
  state transitions), resequencer, coalescing commit-lane ordering logic,
  batch-trigger policy (N files / 2 s), footer summary formatting.
- **Integration** (real vault): full-pipeline import of images + videos; CBZ page
  ordering; collision skip; cancel mid-task; simulated crash between batches →
  reopen shows exactly the last-committed batch, compact reclaims orphans.
- `scripts/test.sh` and `scripts/test.sh --asan` green (threading + crypto
  touched); periodic valgrind note in README applies. TSAN CI job (Phase 42)
  covers the new threading.

## Deliverables

- New: `src/ui/import_queue.*` (task/worker/pipeline), `src/ui/import_status_screen.*`,
  pure `src/ui/import_model.*` (queue + summary models).
- Vault: read handle, `stage`/`attach_staged`/commit-lane additions, bounded-slice
  appends.
- Retired: `ZipImportJob`, `FileOpJob::start_import` (executors reused).
- ROADMAP Phase 50 entry; Serena memory updates (`mem:module/ui`,
  `mem:module/vault`, `mem:module/app`, `mem:core` commit-lane invariant note).
- One PR; owner merges.

## As built (deltas)

Deviations from the design spec, all approved during implementation:

- **Whole-chunk write-mutex holds (no bounded slices).** The spec proposed ~8 MiB bounded slices to keep main-thread commits snappy; implemented as full chunks under mutex (simpler and interleaving hazards ruled out). Main-thread commit waits are acceptable because chunks average <10 MiB and worker is I/O-bound (disk read slower than encrypt/write).

- **CommitLane owns its own thread and is stop-aware.** Lane runs on a jthread owned by ImportQueue, refuses enqueue/flush when stopped (prevents enqueueing into a dead thread), halts gracefully on Vault::lock()/reset() auto-stop before key wipe.

- **Coalescing single-slot pending, generation-ordered.** Lane buffers exactly one pending blob (not N); each generation replaces the prior, ensuring newest commits always win even if the worker thread lags behind burst enqueues.

- **Vault: read_fp_ (second read-only unbuffered handle), write_mutex_ + header_mutex_ (for slot-field contention).** All read paths (thumbnail decrypt, full-image fetch, VideoSource) moved to read_fp_, eliminating contention with worker appends on the main write FILE*.

- **Staging.h: stage_image/stage_video/attach_staged/ensure_gallery_path.** stage_* run on any thread, fflush (no fsync); attach_* main-thread only, no commit issued (commit scheduled separately by batching policy). Index I/O split into serialize_plain_index (memory, fast) and commit_plain_blob (write + fsync).

- **ImportQueue: lookahead cap 8 items/256MiB, per-session state reset.** Strict in-order resequencing; lookahead bounded to prevent unbounded decode heap growth. Per-session reset clears stale tasks/records to prevent old-vault orphan attachments.

- **ImportStatusScreen, footer, Shift+I flow.** UI shows per-item cancel/reorder, lane-failure banner for hard stops, scrolling queue list. Footer priority: error > import summary > status. Exclusive-op guards ("Imports running — press Shift+I for status") block delete/transfer/combine/compact.

- **App: drain() each frame, Screen::on_vault_changed() broadcast (4 overrides).** Grid, viewer, favorites, advanced-search all override on_vault_changed to refetch cached IndexNode* refs after tree mutations.

- **Password-at-enqueue for encrypted archives.** Wrong password discovered mid-import fails the task; no re-prompt.

- **Batched commit durability trade-off.** Crash mid-batch loses at most the last uncommitted batch's index entries; orphaned chunks are dead ciphertext reclaimed by Shift+C compact. 3-phase slot swap unchanged; integrity guaranteed.
