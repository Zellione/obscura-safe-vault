# Phase 60 — In-place vault compaction (dead-space packing)

Full design rationale: `docs/superpowers/specs/2026-08-02-phase60-in-place-compact-design.md`

## Problem

`Vault::compact()` (Shift+C, and the non-Linux `auto_reclaim_space` fallback) rewrites every live chunk into `vault.osv.compact` and renames it over the original. A 200 GB vault therefore needs ~200 GB of *additional* free disk to compact at all. `Vault::reclaim()` (Linux, PR #119) frees physical blocks by hole-punching dead spans, but the file stays sparse: logical size and `wasted_bytes()` never shrink, and copying the vault (backup, USB, cross-machine) re-expands the holes.

**Goal:** a true logical shrink — in place, O(1) extra disk, on every platform — that is cancellable and resumable, and replaces the copy-rewrite compact everywhere.

## What changed

**Core invariant:** No byte referenced by the last-committed index is ever overwritten. Every write during compaction lands in space that is dead according to the index a crash would reload. Therefore every crash window — including power loss mid-write — leaves a valid, merely partially-compacted vault. There is no journal, no sidecar file, and no recovery code at unlock.

**Algorithm** (in-place dead-space packing, `Vault::compact(OpProgress*)`):

1. **Quiesce:** flush CommitLane; hold `write_mutex_` for the duration (the progress modal already blocks imports; this makes it airtight).
2. **Plan:** copy the index root; collect movable units (image data span, thumb span, each 1 MiB video chunk, poster) as `{node, span-kind, offset, length}`. The active index blob is not a movable unit; it is superseded by batch commits.
3. **Pack** — loop until a full pass makes no move:
   - **Slide pass:** walk live units in ascending offset order; when a dead gap precedes a unit and `dest + len <= src`, stream-copy it down through a bounded 1 MiB buffer and update the copy's span.
   - **Tail-fill pass:** take units from the end and first-fit them into earlier dead holes they fully fit in. Destination is entirely dead, so this is always safe.
   - **Batch commit** (every ~256 MiB moved): fsync data, serialize the tree copy, seal, append blob, write inactive slot, fsync, flip `active_slot` — existing `index_io` 3-phase primitives, driven synchronously. Order is load-bearing: chunk bytes are durable before any index references them.
4. **Final commit:** write the last index blob into dead space immediately *after* the last live media byte, flip, fsync.
5. **Shrink:** truncate the file after the final blob (`fileutil::truncate_file`); hole-punch any residual stuck holes (Linux).
6. **Publish:** swap the tree copy into `root_`, update `header_`.

Moves are byte-verbatim `nonce|ciphertext|tag` copies — no decrypt, no re-encrypt, security invariant #1 untouched.

**Cancel** is checked between moves: on cancel, run the final commit + truncate and return `Ok` — everything moved so far stays moved. **Resume** is just running compact again — there is no persisted state, so re-running compact at any point continues from the current state.

**Convergence:** every move strictly reduces either leading dead space or the last-live-byte position; a pass with zero moves exits the loop. Residual holes smaller than every chunk positioned after them cannot be filled, are hole-punched (physical blocks freed on Linux), and remain as bounded logical slack.

**Integration & UX:**

- **Shift+C flow unchanged:** confirm → `FileOpJob::start_compact` → worker thread → progress modal (progress is byte-based: `done/total = bytes moved / bytes to move`). Esc cancels but keeps progress; completion reports bytes reclaimed.
- **Copy-rewrite machinery deleted:** `copy_compact_chunks`, `count_compact_chunks`, `relocate_node_chunks` (vault_ops), the `.compact`/`.old` 3-step rename sequence, and the handle-reopen recovery blocks. File handles stay open throughout — a large net simplification.
- **`auto_reclaim_space()`:** Linux keeps cheap `reclaim()` (hole punch); non-Linux switches from copy-compact to in-place compact — Windows deletes stop transiently doubling disk use. `reclaim()` itself is unchanged.

## Tests

New tests cover: planner property test, in-place no-second-file watcher, stuck-hole residual bound, sync-failure sweep with cold reopen, cancel-keeps-progress + rerun-converges, framed round-trip. Vault-level tests verify correctness under imports and deletes. No `INDEX_VERSION` bump — compact is internal.

1724 tests / 0 failed; ASAN clean.
