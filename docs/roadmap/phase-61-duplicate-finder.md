# Phase 61 — Duplicate finder

Full design rationale: `docs/superpowers/specs/2026-08-03-duplicate-finder-design.md`

## Problem

A vault that has grown through years of archive imports accumulates duplicate
media — the same file imported into different galleries, or near-identical
copies (re-saves, resized re-downloads). There was no way to find them short of
eyeballing thumbnails; deleting them one at a time costs one fsync'd commit per
file.

**Goal:** a manually triggered whole-vault duplicate scan — exact and
optionally perceptual — with a side-by-side review screen and a single-commit
batch deletion of the not-kept copies. No `.osv` format change, no hash
caching, all six security invariants intact.

## What changed

**Trigger:** `Ctrl+D` on the gallery grid (`D` alone toggles the detail
panel). It is an exclusive op behind the same import-queue gate as compact; a
new "Vault tools" F1 help group documents `Shift+C` and `Ctrl+D`.

**Scan** (`ui/dup_scan`): the index tree is main-thread-only, so
`ui::collect_scan_items` snapshots every media node (path, type, byte size,
thumb/poster span) on the main thread before the worker starts. The background
`DupScanJob` thread then only ever calls the thread-safe
`vault::read_thumb_span` — a generic chunk-span decryptor that works for data
chunks, video chunks, and thumbnails alike — never the tree.

- **Exact pass (images + videos):** group by (type, byte size) using snapshot
  metadata only; for each group with ≥ 2 members, decrypt the full content
  into an mlock'd `crypto::SecureBytes`, hash with Monocypher's incremental
  `crypto_blake2b_*`, wipe immediately. Equal size + equal hash ⇒ `Identical`
  group. The size pre-filter means only files sharing a size are ever
  decrypted.
- **Perceptual pass (optional, images only):** for each image not already in
  an exact group, decrypt and decode its **stored thumbnail** (never the
  original), downscale to 9×8 grayscale, compute a 64-bit dHash, union-find
  cluster at Hamming distance ≤ 5. Groups tagged `Similar (N bits)`.
- Undecryptable/undecodable files are skipped and counted ("couldn't examine
  N files"), never abort the scan. Manual lock mid-scan cancels the worker
  before key wipe (quiesce-before-wipe); a `Locked` read result ends the scan
  gracefully. Hashes are session-lifetime heap data — never persisted, never
  logged.

**Review** (`ui/dup_model` + `DuplicatesScreen`): the screen opens in a
chooser state (exact / exact + visually similar), shows scan progress with
graceful Esc-cancel, then a scrollable list of groups sorted
largest-reclaimable-bytes first. Each group is a horizontal row of
side-by-side tiles (thumbnail or poster, name, parent path, size, resolution)
with KEEP/REMOVE badges, all starting KEEP. `Left/Right`/`Up/Down` move focus,
`Space` toggles, `A` keeps only the focused member, `Enter` full-screen
inspects the decoded original. The at-least-one-KEEP group invariant is
enforced in the pure model (a group marked all-REMOVE renders in a warning
state and blocks apply) — deleting every copy would be plain deletion, not
de-duplication. Esc with unapplied REMOVE marks prompts first.

**Apply:** `Ctrl+Enter` → default-cancel confirm with count + total size →
one main-thread call to the new vault free friend `vault::remove_media_batch`:
N tree erases, ONE `commit_index()`, one `auto_reclaim_space()` — one
crash-safe slot swap instead of one fsync per file. Missing/non-media paths
are counted in `RemoveBatchStats`, not errors. A Done state reports what was
removed; the grid refreshes via `on_vault_changed()`.

`dup_model` is pure and SDL-free (dHash primitives, union-find clustering,
grouping, marking state, wasted-bytes sort); `Vault` stays at its cpp:S1448
method cap by keeping `remove_media_batch` a free friend.

## Tests

`tests/ui/test_dup_model.cpp` (dHash KATs, Hamming clustering, grouping,
marking transitions, group invariant, sort), `tests/ui/test_dup_scan.cpp`
(snapshot collection, planted exact + near-dupe vaults, skip path, cancel),
`tests/vault/test_remove_batch.cpp` (batch removal, missing paths, one-commit
semantics, locked vault). No `INDEX_VERSION` bump — nothing is persisted.

1757 tests / 0 failed; ASAN clean; TSAN reports no races in project code (the
only findings on the dev box are pre-existing Mesa `radeonsi_drv_video.so`
driver-internal races under the Phase 43 video-decode tests).
