## Phase 69 — Batched cross-vault transfer commits ✅

**Goal:** Make vault-to-vault transfer (Move/Copy/Combine) stop being an order
of magnitude slower than import. The import queue batches durability through
the CommitLane (one commit per 32 files / 2 s); the transfer path paid, **per
file**: one chunk fsync + one synchronous `commit_index()` at the destination
(full index serialization + 3-sync slot swap), and in Move mode a second
`commit_index()` on the source via `remove_image` **plus** an
`auto_reclaim_space()` pass. That last one compounds: on Linux `reclaim()`
punches holes but never lowers the *logical* `wasted_bytes()` measure, so once
a large move crosses the ¼-waste gate, **every subsequent per-file remove
re-runs the full live-span scan + hole-punch** — O(files × vault-size).
A 10-file Move cost ~70 fsyncs; a 1,000-file move spent minutes on pure
commit/reclaim overhead that import never pays.

### What changed

- **Staging ingress split** (`src/vault/staging.*`):
  `attach_image_prestaged` / `attach_video_prestaged` do the Phase 67
  pre-check → stage → attach sequence **without** the trailing fsync + commit;
  `commit_staged(Vault&)` makes everything attached since the last commit
  durable in one chunk fsync + one crash-safe `commit_index()`. The Phase 67
  `add_*_prestaged` functions recompose from these (attach + commit), keeping
  the single-file `transfer_image` contract identical.
- **Batched bulk drivers** (`src/vault/transfer.cpp`): `CopyCtx` carries the
  batch state (`pending` copied-but-uncommitted source paths, `moved`
  committed paths awaiting removal). `flush_dst` commits once per
  `TRANSFER_COMMIT_BATCH = 32` files (mirroring the import queue's batch
  size); `finish_copies` runs on completion *and* cancel: final flush →
  `lower_dst_watermark` → deferred Move removals.
- **Deferred Move removals:** one `vault::remove_media_batch` for all moved
  files after the destination commit — a single source commit + a single
  `auto_reclaim_space()` pass instead of one of each per file. Files whose
  destination add failed are never removed (per-file precision preserved).
- **Gallery recreation rides the batch:** `copy_subtree` uses
  `ensure_gallery_path` (idempotent, no commit) instead of `create_gallery`
  (which committed per gallery).
- **Combine routed through the batched driver:** `move_media_children` calls
  `transfer_images` with `TransferProgress{.set_total = false}` (combine
  manages a subtree-wide progress total that the bulk driver must not
  clobber) instead of looping the committing `transfer_image` per file.
- **Observability:** `fileutil::sync_call_count()` — an atomic counter bumped
  in `fileutil::sync()`, following the `inject_sync_failure` test-hook
  convention — lets tests pin the batching behavior.

### Semantics (unchanged where it matters)

- **Destination durability strictly precedes any source mutation.** A crash
  mid-transfer loses at most the uncommitted batch (files still in the
  source, staged chunks are orphaned dead ciphertext reclaimed by compact —
  the Phase 50 import contract) or leaves already-committed files briefly in
  both vaults (recoverable duplicates). Never a loss.
- **A batch-commit failure fails exactly that batch's files** (recorded in the
  tally with stage Write) and hard-stops the transfer; earlier committed
  batches are unaffected, and none of the failed batch's files is removed
  from the source.
- **Cancel** stops between files, then `finish_copies` still flushes the files
  copied so far and (for Move) removes them from the source — items moved so
  far live only in the destination, the rest only in the source, no
  duplicates.
- Empty-gallery pruning after a gallery Move is unchanged (runs after the
  batched removal, skipped on cancel).

### Acceptance criterion ✅

`tests/vault/test_transfer_batching.cpp`: a 10-file Copy stays ≤ 9 fsyncs and
a 10-file Move ≤ 12 (previously ~40 / ~70); attach-without-commit does not
survive a reopen while `commit_staged` persists it; a Move with a collision
removes only the successfully-copied sources; an injected commit failure fails
the whole batch with the source intact and the destination clean on reopen; a
gallery Move and a combine merge keep their end-state semantics under bounded
sync counts. Full suite: **1902 tests / 0 failed**, ASAN clean.

No `.osv` change, no `INDEX_VERSION` bump.
