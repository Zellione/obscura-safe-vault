# Module: vault/ + crypto/ — storage & security core

Referenced from `mem:core`. Covers `src/crypto/` (Monocypher wrappers) and `src/vault/`
(the `.osv` container format). Galleries may hold any mix of images, videos, and
sub-galleries — no leaf-only restriction (the old insertion guards were removed).

## crypto/
- `aead.*`, `kdf.*`, `random.*`, `secure_mem.h`, `crypto.h` — Monocypher wrappers:
  XChaCha20-Poly1305 AEAD, Argon2id KDF, platform CSPRNG (getrandom/BCryptGenRandom),
  `SecureBytes`/mlock buffers, `crypto_wipe`. macOS unsupported (`#error` guard in
  `random.cpp`).

## vault/ — `.osv` container
Core files: `vault.*`, `header.*`, `index.*`, `chunk_store.*`, `byte_io.h`, `file_util.h`.

### file_util.h — position-independent size query (PR #109, durability)
`fileutil::file_size` MUST be position-independent (`fstat`/`_fstat64` on the fd), NEVER
`seek_end`. WHY: `write_header` does `seek_to(fp_,0)` then `fwrite` as two separately-locked
stdio calls; `Vault::wasted_bytes` calls `file_size(fp_)` on the MAIN thread WITHOUT
`write_mutex_`, concurrently with the commit lane. A seek-based size query landing between the
header's seek-to-0 and its write moves the shared FILE*'s offset, so the header (active_slot +
slot pointers) is written at end-of-file instead of offset 0 → reopen loads a stale index →
silent data loss. TSan can't see it (the shared state is the FILE* offset, libc/OS state, not
memory), and it is load-sensitive. Consequence: `seek_end` used to flush the stdio buffer as a
side effect, so `ChunkStore::append_at_end` now `fflush`es after its write (every production
caller already flushed, so zero prod impact) to keep read-after-append on the same handle
working. Guarded by `tests/vault/test_file_util.cpp` (position-neutrality + a concurrent
seek0-writer/size-reader race that misplaces writes on the old impl).

Also in file_util.h (PR #119): `fileutil::punch_hole(fp, off, len)` — Linux
`fallocate(FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE)` (a no-op returning false elsewhere) —
and `fileutil::file_allocated_bytes` (`st_blocks * 512`, physical size for sparse-aware waste
measurement). Phase 60 adds `fileutil::truncate_file(fp, new_size)` (fflush + ftruncate/_chsize_s); the rename
fault-injection family, `wipe_and_remove`/`wipe_file_contents`, and `sync_dir_of` were deleted with
the copy-rewrite compact (sync fault-injection `inject_sync_failure` remains — the compact
crash-sweep test uses it). See the reclamation note below.

### Reclamation — compact() vs. reclaim() (PR #119)
Two ways to reclaim orphaned chunk space (deleted chunks + the superseded index slot):
- `Vault::compact()` — **Phase 60: in-place dead-space packing** (no temp file, no rename, O(1)
  extra disk; the old ~2x copy-rewrite is gone). Cardinal invariant: **no byte referenced by the
  last-committed index is ever overwritten.** Pure planner `compact_plan.*` (`Unit`/`Move`,
  `plan_pass`, `live_end`) computes moves into a FROZEN free list (dead space of the committed
  layout; tail-first, earliest-fit; active index blob pinned; space vacated by a pass is only
  reused after the next commit legalises it; property-tested in `test_compact_plan.cpp`).
  compact() quiesces the CommitLane, holds `write_mutex_` throughout, mutates a COPY of the
  tree, streams byte-verbatim ciphertext moves (1 MiB slices via `ChunkStore::write_raw_at` —
  never a decrypt), batch-commits every 256 MiB + per pass via `index_io::commit_index`, places
  the FINAL blob just after the packed data (`index_io::commit_plain_blob_at`; falls past the
  active blob on overlap — one blob of slack, reclaimed next run), truncates
  (`fileutil::truncate_file`), punches residual stuck holes (holes smaller than every later
  unit = bounded logical slack), then publishes the copy into root_. Crash at ANY instant
  reopens to a valid partially-compacted vault; rerun resumes; cancel keeps progress (returns
  Ok); an idempotent short-circuit makes compacting a tight vault a true no-op. Progress is
  MiB-moved (done <= total). Invalidates IndexNode pointers. Still the manual `Shift+C` path
  (gated on `queue_.busy()`) and the non-Linux auto-reclaim fallback — Windows deletes no
  longer spike 2x disk. Helpers: file-local `collect_units`/`stream_move`/`execute_pass_moves`
  + `MoveExecState` (S107 cap). DELETED with the copy-rewrite: `relocate_node_chunks`, rename
  fault-injection (`rename_file`/`inject_rename_failure`), `wipe_and_remove`/
  `wipe_file_contents`, `sync_dir_of`, and the `.compact`/`.old` 3-step rename dance.
- `Vault::reclaim()` (Linux) — punches holes over the DEAD spans in place (dead = complement of
  the live-span set: active index blob + every image data/thumb + video chunks/poster).
  Offset-stable (no index rewrite, no reopen, pointers stay valid), no temp copy → NO disk spike.
  Crash-safe by construction (only dead bytes touched). Freed blocks return to the FS; the file
  stays SPARSE, so logical size and `wasted_bytes()` (a LOGICAL measure) are unchanged — physical
  disk drops but the waste indicator still shows the holes. `Locked` if locked; `Ok` (no-op) where
  hole-punch is unsupported. Serialized against the CommitLane: `flush()` the lane, hold
  `write_mutex_` across scan+punch, snapshot the active slot under `header_mutex_`. Span collection
  lives in the free helper `collect_media_spans` (keeps reclaim() under the cpp:S3776 complexity cap).
- `Vault::auto_reclaim_space()` — the shared best-effort gate (thresholds `AUTO_COMPACT_MIN_WASTE`
  + `AUTO_COMPACT_WASTE_RATIO`) called by `remove_image`/`remove_gallery`: `reclaim()` on Linux,
  else `compact()`. Tests: `tests/vault/test_vault_compact.cpp` (reclaim preserves media + survives
  reopen, frees blocks without shrinking logical size, `Locked` when locked; physical assertions
  guarded by a runtime hole-punch probe) and `test_file_util.cpp` (punch_hole zeroes + frees blocks; truncate_file). Phase 60 adds:
  in-place watcher (no sibling temp file, peak size bounded to one commit's overhead), stuck-hole
  residual bound (<=32 KiB), sync-failure sweep (inject n=0..11 → cold reopen valid + rerun
  converges), cancel-keeps-progress + rerun-converges, framed round-trip; the planner's
  crash-safety contract is property-tested in `tests/vault/test_compact_plan.cpp` (200 random
  layouts: dests dead per frozen layout, pairwise disjoint, strict progress, termination).

### Phase 50 concurrency: "main-thread tree" architecture
The index tree is **main-thread-only**; no tree locks exist. The vault file opens two handles + one write-path mutex for thread-safe import background queue:
- **`read_fp_`** — second read-only unbuffered FILE* (opened at unlock, closed+wiped at lock). All read paths move to it: thumbnail decrypt, full-image fetch, `VideoSource` (chunks are immutable once appended, so reads never race worker appends). No contention with worker writes.
- **`write_fp_` + `write_mutex_`** — original write handle guarded by a std::mutex. Worker appends chunks under lock (whole chunks, no bounded slices to avoid interleaving hazards).
- **`header_mutex_`** — separate guard for slot-field mutations during commit (active_slot flip + generation update), to reduce lock hold time contention.
- **`Vault::lock()` auto-stops the CommitLane** before key wipe, preventing mid-flight commits after the key is gone. **Phase 94 (OSV-AUD-001):** lock() then clears `settings_ = VaultSettings{}` too — a lock is a complete decrypted-state boundary; the category names / descriptions / template fields / field values are index-derived plaintext that must not outlive the key and tree wipe. A re-unlock re-populates `settings_` from the index slot.

### Phase 58 concurrency: `thumb_fp_` + `thumb_mutex_` (async-safe thumbnail reads)
- **`thumb_fp_`** — THIRD dedicated file handle, separate from `read_fp_`, for thread-safe
  `read_thumbnail()`/`read_thumb_span()` calls from ANY thread (UI threads, decode workers, etc.).
  WHY: decouples thumbnail I/O from the main index/data read path, eliminating render-thread file
  seeking that causes 0.5–1 s UI latency on large vaults (50k+ items); ThumbnailKey resolves the
  offset/length span, worker thread does the seek+read, render thread never touches the vault.
  Opened at unlock (before key is available), closed at lock.
- **`thumb_mutex_`** — guards all `thumb_fp_` access. `reset()` (called at lock-time) flips
  `unlocked_` state and closes `thumb_fp_` UNDER the mutex BEFORE `crypto_wipe(master_key_)` —
  **quiesce-before-wipe ordering** prevents a concurrent `read_thumbnail` call from waking up
  post-lock and trying to use a wiped key. `unlocked_` is checked BEFORE dropping the lock in
  read_thumbnail, and if false, the call returns `Locked`. The mutex is never held during I/O —
  it guards only the state check/file operations.
- **`resolve_node` promoted to public** (Phase 58) — necessary for thumbnail cache (ThumbKey)
  to build and validate node identities without exposing internal tree navigation.
- Consequence: `read_fp_` now handles **only** full-image data and video chunks; all thumbnail
  I/O and metadata header reads flow through the dedicated, contention-free `thumb_fp_`.

### staging.* (Phase 50 + Phase 67 transfer + Phase 69 batching) — worker-to-tree hand-off
- `stage_image(Vault, data, ...)` / `stage_video(Vault, data, precomputed_meta, ...)` — any thread, stream encrypted chunks to disk with fflush (no fsync), return a ready IndexNode with chunk spans. Plaintext stays mlock'd; nodes are ready but **not attached**. **Phase 67:** `stage_video` gains a 5th `precomputed_meta` param (a `StagedVideoInfo{dimensions, duration, codec, poster_offset, poster_len}`) so cross-vault transfers avoid re-probing videos already decoded at the source. Null metadata falls back to `MediaProbe::probe_bytes` on attach (backward compat). `StagedVideoInfo` is computed at source by `MediaProbe::extract_staged_video_info`.
- `attach_image_prestaged` / `attach_video_prestaged` — **free friends, Phase 69.** Main-thread only: pre-check (safe name, gallery exists, no sibling collision) → stage → attach, **no fsync, no commit** — bulk transfer loops attach many files and batch durability. **Phase 75:** all four `attach_*`/`add_*_prestaged` take a trailing `const NodeExtras* extras = nullptr` (`vault::NodeExtras{std::vector<std::string> tags; bool favorite;}`, staging.h) applied to the node BEFORE tree insertion (tags capped at INDEX_MAX_TAGS by truncation) — transferred tags/favorites ride the same batched commit, never a separate one. On a crash before the next commit the attached nodes vanish; their chunks are orphaned dead ciphertext reclaimed by compact (same contract as a cancelled import).
- `commit_staged(Vault&)` — **free friend, Phase 69.** One chunk-store fsync + one crash-safe `commit_index()` for everything attached since the last commit.
- `add_image_prestaged(Vault, ...)` / `add_video_prestaged(Vault, ...)` — **free friends, Phase 67.** Single-file convenience: `attach_*_prestaged` + `commit_staged` (per-file durable commit). Used by `transfer_image` (the single-file primitive); bulk loops use the attach/commit split instead.
- `attach_staged(Vault, node, gallery_path)` — main-thread only, performs tree insertion, **no commit issued**. Commit is scheduled separately by batching policy (see CommitLane below).
- `ensure_gallery_path(Vault, path)` — creates missing ancestor galleries as needed on attach.

### safe_name.* — node-name rules (a node name is a single path COMPONENT, never a path)
- `is_safe_node_name` = REJECT. Vault ingress trust boundary: `add_image`/`add_video`/
  `create_gallery`.
- `sanitize_node_name` = REPAIR. Importers (zip_plan basename/dir components, meta_json
  titles, file_op_job picked files) — an awkward archive name must not fail a whole import.
  Output ALWAYS satisfies `is_safe_node_name` (property-tested).
- Rejects `/` and `\` on every platform, `.`/`..`, NUL/control/DEL, Windows-reserved
  chars + device names (CON/NUL/COM1-9/LPT1-9), trailing dot/space, >255 bytes (truncated
  on a UTF-8 codepoint boundary). Bytes >=0x80 stay opaque (CJK).
- WHY: a `.osv` is UNTRUSTED INPUT (portable/shareable). `index.cpp` reads `name` as opaque
  bytes; `ui::export_*` turns it back into a real path. `dest_dir / name` does NOT contain
  — an ABSOLUTE name discards `dest_dir` (CWE-22). The sink is therefore ATOMIC (Phase 98):
  `platform::create_new_file_within(dest_dir, sanitize_node_name(name))` claims the file with
  an exclusive no-follow create (Linux openat2 RESOLVE_BENEATH|NO_SYMLINKS / openat O_NOFOLLOW,
  Windows CREATE_NEW + final-handle containment) and `export_one_media` writes to the returned
  handle only. `export_path_within` (weakly_canonical + lexically_relative, fails closed)
  remains as a post-create invariant assertion because vaults already on disk can carry
  hostile names — ingress validation alone is not enough.

### Free friends (kept off Vault to stay under the cpp:S1448 35-method cap)
- `vault::read_thumb_span(v,offset,length,out)` — decrypt a thumb/poster chunk by raw span
  (gallery cover montages); InvalidArg if len 0, Locked, AuthFailed on tamper.
- `vault::gallery_sort_key(v,path)` / `vault::set_gallery_sort(v,path,SortKey)` — persisted
  via commit_index. `Vault::list` resolves the target gallery's stored sort_key against the
  vault-wide default through `ui::effective_sort_key` before returning, so every caller (grid,
  list view, viewer strip, slideshow) gets one order for free. Pure ordering logic lives in
  `ui/gallery_sort.*` (see `mem:module/ui`).
- `vault::vault_settings(v)` / `vault::set_vault_settings(v,VaultSettings)` (Phase 49) —
  vault-global settings; the setter persists through the same crash-safe commit_index swap and
  returns `Locked` on a locked vault. Held in `Vault::settings_`; `reset()` clears it,
  `create()` seeds it, and `lock()` clears it. **Phase 94 (OSV-AUD-001):** the getter returns an
  immutable process-wide empty object while `!unlocked_` (locked or not-yet-unlocked), never the
  retained copy — defense in depth over the lock-time clear. **Both move operations must carry
  `settings_`** — they originally
  omitted it, silently dropping settings on a move (fixed, regression-tested).
- `vault::find_tag_description(settings, tag_name)` / `vault::set_tag_description(settings, tag_name, description)` (Phase 51) —
  key-value store on a `VaultSettings` (NOT Vault methods — `Vault` is at its `cpp:S1448` 35-method cap). Keys matched
  case-insensitively via `ui::tag_ci_equal` (first-seen casing kept, matching `add_tag`); an
  empty description removes the entry. No default return value — these are bare operations on the
  settings object. Persisted via the existing crash-safe commit via `set_vault_settings`.
- `vault::remove_media_batch(v, span<const string> node_paths, RemoveBatchStats*)` (Phase 61,
  duplicate finder) — erases every media node named by full slash-path, then ONE
  `commit_index()` (none if nothing removed) + one `auto_reclaim_space()` — one crash-safe
  slot swap instead of one fsync per file. Missing / non-media paths are counted in
  `RemoveBatchStats{removed, missing}`, not errors. Locked if locked; IoError if the commit
  fails (tree already mutated — same contract as `remove_image`'s failed commit). Main-thread
  only (mutates the tree). Tests: `tests/vault/test_remove_batch.cpp`.
- `vault::remove_nodes_batch(v, span<const string> node_paths, RemoveBatchStats*)` (Phase 74
  multi-select delete, free friend beside remove_media_batch — which stays media-only and
  unchanged) — same one-commit + one-auto-reclaim contract, but a path may name a media node
  OR a gallery subtree (`erase_any_child`; a gallery erase orphans every descendant chunk,
  exactly like `remove_gallery`). Missing paths and the un-erasable root ("") are counted in
  stats, not errors. Called from `FileOpJob::start_delete_batch` on the file-op worker (the
  worker owns the vault handle while active — the same threading contract as every FileOpJob
  op, so "main-thread only" is satisfied by exclusivity, not literal thread identity).
  Tests: `tests/vault/test_remove_batch.cpp`.
- `vault::set_favorites_batch(v, span<const string> node_paths, bool value)` (Phase 68
  multiselect, free friend beside remove_media_batch) — sets every resolving path's favorite
  flag (galleries and media alike) to `value`, ONE `commit_index()` — and none at all when no
  node actually changed. Non-resolving paths are skipped, not errors. Locked if locked;
  IoError if the commit fails. Main-thread only. The any-unfavorited→favorite-all TOGGLE rule
  lives UI-side (`ui::batch_favorite_target`). Tests: `tests/vault/test_vault_favorites.cpp`.
- `vault::rename_node(v,gallery_path,old_name,new_name)` — validates `is_safe_node_name` +
  no sibling collision, then a pure leaf-field edit (an IndexNode persists only its local
  name, never a path, so no cascade). Drives the `R` RenameDialog.

### transfer.* + transfer_result.* (Phase 67 + Phase 69 batching) — cross-vault & within-vault copy/move
- **TransferFailure** — reason a single-file transfer failed (ASCII enum for display):
  `{SizeMismatch, NotAnImage, NotAVideo, ProbeFailure, AuthFailure, WriteFailure, Collision, Unknown}`.
- **Phase 71 — collision policy + file-skip rule:** `CollisionPolicy : uint8_t {Fail, Combine, Suffix}` + `GalleryTransferOpts{progress, tally, policy}` (bundled for S107). `transfer_gallery(src, sg, dst, dst_parent, mode, GalleryTransferOpts opts = {})` — when dst_parent already holds a same-named child: Fail → AlreadyExists (default, pre-71 behavior); Suffix → first free `name_2`..`name_9999` (base trimmed on a UTF-8 codepoint boundary to fit MAX_NODE_NAME_BYTES; helpers `trim_utf8`/`unique_child_name`, dispatch extracted into `resolve_dest_collision` for S3776); Combine (colliding child is a gallery) → delegates to `combine_galleries` on the pair, tally maps media_moved→done / media_skipped→skipped; Combine into a MEDIA child → AlreadyExists. Dispatch runs before the progress-total store, so combine owns the progress object. `transfer_galleries(..., progress, policy = Fail)` forwards per subtree. `colliding_galleries(dst, dst_parent, span src_paths)` — main-thread pre-scan for the dialog prompt (matches ANY child, gallery or media; empty while locked). **File skips everywhere:** a Write-stage AlreadyExists in the bulk drivers (`copy_one_media_ex`) increments `TransferTally.skipped` and is NOT recorded in failures (file stays in the source); single-file `transfer_image` still returns AlreadyExists. `combine_galleries` gained a trailing `TransferMode mode = Move`: Copy copies missing files/subtrees, skips collisions, unions tags, and NEVER deletes source galleries or media (deletion gate `mode == Move && list(src).empty()`); combine.cpp bundles its recursion state in `MergeCtx` (S107). Tests: `tests/vault/test_transfer_conflicts.cpp`.
- **TransferTally** — per-file transfer status (`Success`, `Failed(reason)`, `Skipped`).
- **TransferCompletion** — post-transfer outcome: `status` (Ok/Locked/IoError), `destination_path`, 
  `counts{total_files, succeeded, failed, skipped}`, `failures` (vec of `{name, reason}` for UI modal). 
  `name` is the node's local name only (no path).
- **Per-file transfer tolerance:** `transfer_image` + `transfer_gallery` + `transfer_galleries` (the three 
  public primitives) process each file independently: a collision (destination sibling exists) or a probe 
  failure (video undecodable at source) fails that file, records its reason, and continues. At the end, 
  a `TransferCompletion` summarizes counts + failures list. Callers decide failure policy (UI shows 
  modal with reasons; batch script may fail/ignore). An Ok status means at least one file transferred; 
  a Skipped or Failed file does not prevent Ok return.
- **Move with prune_moved_galleries (Phase 67):** `transfer_gallery` and `transfer_galleries` each take a 
  `prune_moved_galleries` bool. True: after transfer completes, deletes every source gallery that 
  (a) was explicitly moved (not copied) AND (b) is now empty (all its media was transferred away). 
  Rationale: user moves a folder + its contents to another vault; we auto-clean the empty shell. 
  Pruning happens AFTER the final commit (transfer itself is atomic; pruning is best-effort and 
  happens in a second pass if `!status.is_error()`). Pruning is logged to `RemoveBatchStats`; 
  transfer status unaffected by prune results.
- **Batched commits (Phase 69):** the bulk drivers (`transfer_images`, `transfer_gallery` via
  `copy_subtree`, and combine's `move_media_children` which routes through `transfer_images`
  with `TransferProgress{.set_total=false}`) do NOT pay a durable commit per file. `CopyCtx`
  carries batch state (`pending` copied-but-uncommitted src paths, `moved` committed paths
  awaiting removal, `dirty`, `commit_failed`); `flush_dst` runs ONE `commit_staged` per
  `TRANSFER_COMMIT_BATCH = 32` files (import-queue batch size); gallery recreation uses
  `ensure_gallery_path` (no per-gallery commit — rides the batch); `finish_copies` (runs on
  completion AND cancel) does final flush → `lower_dst_watermark` → ONE `remove_media_batch`
  for all Move removals (single source commit + single auto-reclaim — the per-file
  `remove_image` used to re-trigger a full `reclaim()` scan per file once waste crossed the
  gate, since reclaim never lowers logical `wasted_bytes()`). Destination durability strictly
  precedes any source mutation; a batch-commit failure fails exactly that batch's files
  (recorded, stage Write) and hard-stops; earlier batches stay committed and their Move
  removals still apply. `transfer_image` (single-file) keeps its per-file
  `commit_staged` + `remove_image`. Observable via `fileutil::sync_call_count` (atomic
  counter in `fileutil::sync`, same test-hook convention as `inject_sync_failure`); pinned by
  `tests/vault/test_transfer_batching.cpp` (10-file Copy ≤ 9 syncs, Move ≤ 12, was ~40/~70).
- **Cancel semantics (Phase 67, amended 69):** cancel is checked between files; `finish_copies`
  still flushes files copied so far and removes them from the source (Move) — items moved so
  far live only in the destination, the rest only in the source, no duplicates, nothing lost.
  On resume (same transfer request re-invoked), skip-collisions logic prevents re-transfer.
- **Source metadata hand-off:** `transfer_image`/`transfer_video` receive metadata from the source 
  (image orientation, video dimensions/duration/codec via `StagedVideoInfo`). Videos 
  carry pre-probed codec/dimensions so the destination never re-probes (perf + consistency across 
  unknown-codec moves).
- **Phase 75 — tags + favorites persist on every transfer (Move AND Copy, within- and cross-vault):**
  `copy_one_media` captures `NodeExtras{effective_tags(src, path), node->favorite}` BEFORE the
  destination attach (same dangling-pointer discipline as the `ts` capture) and passes it to the
  prestaged attach; every path funnels through it (single-file `transfer_image` via
  `transfer_image_ex`, bulk `transfer_images`, subtree `copy_subtree`, combine's media moves).
  `vault::effective_tags(v, node_path)` (declared in transfer.h) = own tags first (own casing wins)
  ∪ root tags ∪ ancestor-gallery tags root→parent, ci-deduped via `vault::tag_ci_equal` (moved from
  vault.cpp's file-local `ci_equal` to index.h/.cpp — the single vault-side tag identity).
  **Gallery subtrees:** `GallerySnap` gained `tags` + `favorite` (captured at snapshot time, copied
  strings); the transfer driver overwrites the ROOT snap's tags with `effective_tags(src,
  src_gallery)` (ancestors-above materialize on the root; internal inheritance travels intact);
  after `ensure_gallery_path`, `apply_gallery_extras` UNIONS tags onto the destination gallery
  (ci-deduped, capped, favorite ORed — idempotent on rerun, pre-existing destination tags kept).
  `vault::all_galleries(v)` — every gallery path except root, DFS order, empty while locked (feeds
  the pull dialog's source list). `lower_dst_watermark` also lowers `migrated_thumb_side`.
  Tests: test_transfer_tags.cpp (+ combine assertion in test_combine.cpp); batching sync budgets
  unchanged (extras add zero commits).

### migration.* (Phase 65) — vault upgrade orchestration
- `scan_migration(const Vault&, MigrationStatus*)` — pure tree walk (zero I/O). Detects pending
  migrations by counting videos with `codec == Unknown` and images missing the animated flag
  for formats that support animation. Returns counts and total bytes to be read. Deliberately
  over-counts the animated arm: a genuinely static GIF is indistinguishable from an un-backfilled
  one without decrypting, so it IS real work that must be done once. **The watermark, not the
  detector, prevents recurrence.**
- `MIGRATION_INDEX_VERSION` (u8) — constant 7 (the index version that introduced `animated`).
  Staleness checks `settings.migrated_index_version < MIGRATION_INDEX_VERSION`, never against
  raw `INDEX_VERSION`. Gating on `INDEX_VERSION` would re-trigger on every future version bump.
- `migration_is_pending(const VaultSettings&)` — watermark is stale against
  `MIGRATION_INDEX_VERSION` or `media::PROBE_CAPS_GEN`.
- `apply_video_probe(Vault&, const IndexNode::Video&, VideoMeta&)` — free friend. Re-probes
  a video with `codec == Unknown` and returns Ok without mutating if still undecodable (skipped
  in the pass, watermark still advances). Otherwise fills codec, dimensions, duration, and poster
  span. Does NOT commit.
- `apply_image_animated(Vault&, const IndexNode::Image&, ImageMeta&)` — free friend. Re-sniffs
  an image in a format that supports animation, setting the animated flag if needed. Does NOT commit.
- `commit_migration(Vault&)` — free friend. After all apply_* calls, ONE `commit_index()` to
  serialize the migration work atomically. Called by ui::MigrationJob.
- **Phase 75 thumb arm:** `MigrationScan` gains `size_t thumbs`; `scan_migration(v, bool
  thumbs_stale)`; `migration_pending(s, probe_caps_gen, thumb_side)` adds
  `s.migrated_thumb_side < thumb_side`; `stamp_migrated(s, probe_caps_gen, thumb_side)`. The scan
  counts each image in exactly ONE arm (animated-backfill arm first, else thumbs when stale +
  `thumb_length > 0`; known-codec videos → thumbs when stale; Unknown-codec stays in the videos
  arm) — `ui::migration_job.cpp::collect` mirrors this arm parity so offer counts match job totals.
  New free friends `apply_image_thumb(v, node_path, span thumb_jpeg)` / `apply_video_poster(...)`
  (vault.cpp, beside apply_video_probe): Locked → empty-blob InvalidArg → resolve+type check →
  `write_mutex_` → ChunkStore append+sync → REPOINT the span (they REPLACE existing spans, unlike
  apply_video_probe's fill-only-empty poster arm); NO commit. Superseded chunks are dead
  ciphertext for compact. `Vault::create` stamps `migrated_index_version` +
  `migrated_thumb_side = image::THUMB_MAX_SIDE` (fresh vaults owe nothing; unstamped fresh vaults
  used to get a bogus offer). vault/ may reference image:: (staging already does) but never media::
  — probe caps stay caller-passed.
- **Phase 77 fix:** `apply_video_probe`/`apply_image_thumb`/`apply_video_poster` gained a trailing
  `bool sync = true` param — `sync && !store.sync()` instead of an unconditional fsync. The default
  keeps every existing caller's immediate-durability behavior; `ui::MigrationJob`'s coordinator is
  the only caller passing `false` (see module/ui). Root cause: with `sync` always true, a
  whole-vault Phase 75 thumb regen fsync'd once per item (thousands of items = thousands of
  fsyncs) — the migration coordinator's own final `commit_migration()` sync already flushes every
  buffered append on the same `FILE*`, so per-item syncing was pure overhead, not a safety
  requirement.
- **Transfer rule (Phase 65):** `transfer_image` and `transfer_gallery` (the two primitives
  all cross-vault operations funnel through) now lower the destination's watermark to the source's
  whenever the source is behind. This re-offers the migration if content from an un-migrated vault
  arrives at a migrated destination.

### index.* — the index tree
- **Phase 91 (secure strings):** every human-readable string in the tree —
  `IndexNode::name`, `IndexNode::tags`, `SavedSearch::name`, `TagCategory::name` +
  `fields`, `TagDescription::tag`/`text`, `TagFieldValue::tag`/`field`/`value` —
  is a `crypto::SecureString` (new `src/crypto/secure_string.h`: mlock'd
  best-effort, `crypto_wipe` on destroy, no SSO, copyable, explicit `.view()`,
  `is_locked()`; self-view-safe strong assignment; infallible value operations
  terminate on OOM rather than create corrupt empty copies). `SavedSearch::query`
  is a copyable `crypto::SecureBlob` because its opaque encoding contains
  human-readable tags/group names/name filters. Deserialise builds strings straight into
  `SecureString`; the unlock path's decrypted index blob decrypts into
  `crypto::SecureBytes` (`try_load_slot` open_to + decode_frame SecureBytes
  overload); synchronous commits, compact, query serialization, and CommitLane
  snapshots use `crypto::WipingBytes`, whose allocator wipes old capacities,
  coalesced snapshots, failures, and final destruction. `secure_mem.h` tracks
  locks per OS page so freeing one small allocator neighbor cannot unlock a
  still-live secure string on the same page.
  Read sites use `.view()` / the provided `==`/`<=>` vs `std::string_view`;
  SearchHit/transient working sets copy to plain `std::string`. No `.osv`
  change — bytes are identical.
- `IndexNode` carries `std::vector<crypto::SecureString> tags` + `bool favorite` (gallery + image),
  a `SortKey` u8 (meaningful only on Gallery nodes: Default/NameAsc/NameDesc/DateAsc/DateDesc/
  SizeAsc/SizeDesc/Insertion; out-of-range byte rejected, not clamped, bounded PER VERSION —
  v6/v7 max 6, v8 max 7), and Type::Video + VideoMeta (multi-chunk list + poster).
- `INDEX_VERSION=13` (Phase 99; v12 = `migrated_thumb_side` u16 appended LAST in the settings block, v11 = Phase 73 category-template fields + tag-field-values block). Every node carries a
  `node_id` (u8[16]); `ImageMeta`/`VideoMeta` carry per-chunk `*_id` records and a
  `context_bound` flag; `VideoChunk` carries a logical `sequence`. See
  `mem:vault_format` "Context-bound AEAD". Pre-v13 blobs read zeros /
  `context_bound=false` (all records legacy). Vault-global SavedSearch block after the root (name + opaque
  `ui::AdvancedQuery` blob, `INDEX_MAX_SAVED_SEARCHES=4096`), then the Phase 49 vault-global
  **settings block**, then the Phase 51 **tag-descriptions block**, then the Phase 65
  **migration watermark** — see `mem:vault_format` for their byte layouts. `INDEX_MAX_TAGS=4096`.
  Back-compat: v1–v5 read as empty tags / favorite=false / no saved searches; pre-v8 read with
  `default_sort=Insertion`, tile tags on, and `VaultSettings::seeded()`; pre-v9 read with empty
  descriptions; pre-v10 read with `migrated_index_version=0` + `migrated_probe_caps=0` (never
  migrated).
- Phase 49 types: `TagCategory{std::string name; uint8_t swatch;}` and
  `VaultSettings{SortKey default_sort; bool tiles_show_tags; std::vector<TagCategory>
  categories;}` with a static `seeded()` (8 nhentai-style categories on distinct swatches).
  Caps `INDEX_MAX_TAG_CATEGORIES=256`, `INDEX_MAX_CATEGORY_BYTES=64`, `TAG_SWATCH_COUNT=16`.
  Phase 51 adds: `std::vector<TagDescription> tag_descriptions` where `TagDescription{std::string
  name; std::string description;}`, caps `INDEX_MAX_TAG_DESCRIPTIONS=4096`,
  `INDEX_MAX_TAG_DESC_BYTES=512`. Phase 65 adds: `uint8_t migrated_index_version` +
  `uint16_t migrated_probe_caps` — the vault upgrade watermark, read as 0/0 by pre-v10 blobs.
  `write_settings`/`read_settings` mirror the saved-searches pair — the writer CLAMPS, the reader
  REJECTS out-of-range watermark bytes. `serialize_index`/`deserialize_index` gained 4-arg forms;
  the 2- and 3-arg ones delegate. **Any new call site must use the 4-arg form** — the fuzz harness's
  base blob does, so description bytes are reachable by mutation. `read_settings` gained a `version`
  parameter to govern which blocks are read (v8 stops after category block, v9 continues to descriptions,
  v10 continues to migration watermark).
- Favorites: `Vault::toggle_favorite(node_path)` + flat whole-tree
  `list_favorite_images()`/`list_favorite_galleries()` -> `vector<SearchHit>`.
- Tag API + scoped search: `set_tags`/`add_tag`/`remove_tag(node_path)`,
  `prune_tags(const std::function<bool(std::string_view)>& keep, PruneTagsStats*)` (PR #148:
  removes every tag on every node, root included, that `keep` rejects — ONE `commit_index()`,
  none when nothing matched; stats = `tags_removed`/`nodes_touched`, zeroed on Locked/InvalidArg.
  Predicate is caller policy — the UI passes `ui::tag_has_renderable_text` for the Ctrl+X
  junk-tag cleanup on the tag overview),
  `search(query, SearchScope{Images,Galleries,Both})` -> `vector<SearchHit>`.
  Read-time tag cascade (effective tags = own ∪ ancestor galleries; root tags global).
  `resolve_node` resolves a path to a gallery OR image.

### vault_search.* — VaultSearch facade (friend over Vault&, keeps Vault under S1448 cap)
- `all_tags()` (distinct case-insensitive vocab), `run_search(ui::AdvancedQuery)` ->
  `vector<SearchHit>` ranked by score then path.
- `save_search`/`list_saved_searches`/`delete_saved_search` (upsert by name, persisted via
  commit_index).
- `tag_overview()` -> `vector<ui::TagTally>` (per-distinct-tag direct {gallery,image} counts,
  no cascade), `galleries_with_tag()` / `images_with_tag()` (direct carriers only, no cascade).
  **Deliberately unchanged Phase 51:** direct-only counts are preserved so the Phase 22 rule
  (the cascade cannot inflate overview tallies) stays in force.

### Transfer of search results + tag unions (Phase 51)
- `search_dfs` / `adv_search_dfs` now RETURN the subtree tag union (all tags, own+inherited+descendants,
  cased per first-seen) and thread it bottom-up in the existing single post-order pass. A gallery's
  subtree union is assembled from direct children's unions (no re-descent). Leaves are simple
  (just carry their own tags). The result is used by the search result node_tag(hit) to add
  descendant tags to a gallery match, allowing search to find a gallery that holds a descendant's tags.
  The roll-up is computed for EVERY node during search but only used when a gallery's direct/inherited/own
  tags + subtree union collectively match the query.

### Internal components (extracted from Vault to keep it under the S1448 cap)
- `index_io.*` (Phase 50 split) — index serialisation + crash-safe double-buffer slot swap (append → write
  inactive slot → flip active_slot; 3-phase atomic commit). `IndexIoContext` bundles mutable
  state. **Split into**:
  - `serialize_plain_index(vault, context)` — memory-only, fast, produces a serialized index blob.
  - `commit_plain_blob(vault, blob, generation)` — enqueues the blob to CommitLane with a generation tag for ordered, coalesced writes.
  - `commit_plain_blob_at(ctx, plain, offset)` (Phase 60) — same 3-phase swap but the sealed blob
    is WRITTEN AT a caller-chosen dead offset (`ChunkStore::write_raw_at`) instead of appended;
    compact()'s final commit uses it so the blob doesn't pin the dead tail against truncation.
    Phases B+C are shared with `commit_plain_blob` via the file-local `swap_slots` helper.
- `vault_ops.*` — tree navigation + path resolution + structural validation (split_path,
  resolve_gallery, resolve_node_impl, child_named, holds_media, holds_galleries,
  for_each_media, relocate_node_chunks). Pure traversal, no I/O. `push_child(children,node)`
  wraps the `vector::push_back` in try/catch (alloc failure → IoError, not terminate());
  `push_child_fail_after` mirrors `resize_fail_after` fault-injection for tests.
### commit_lane.* (Phase 50) — batched, ordered index commits
- `CommitLane` owns a jthread + a CV-based work queue. Runs independently, stops gracefully on Vault::lock().
- `enqueue(generation, blob)` → appends if not stopped; dequeues stale blobs (coalescing).
- Main thread serializes index every N=32 files or 2s, tags it with generation, hands to lane.
- Lane writes the inactive slot, fsyncs, flips active_slot in generation order — newest blob always wins.
- Write failure is a hard stop: queue halts, error surfaces on status page; already-committed work safe.
- Commit-lane flush runs on queue drain, cancel, lock, and shutdown (ordered final write before key wipe).

- `chunk_codec.*` — pure adaptive store-if-smaller deflate framing: method byte (0=raw,
  1=deflate) + bounded `orig_len` inside the AEAD; used by ChunkStore's framed ctor flag
  (← header `FLAG_FRAMED_CHUNKS` bit) + the index-blob sites. miniz tdefl/tinfl, no new dep.
  The `resize_buf` overload wraps `resize()` in try/catch (it is noexcept; an uncaught
  alloc-failure exception there would terminate() the process); `resize_fail_after` fault
  injection makes the failure path deterministically testable. Legacy vaults (header flag
  unset) read AND append raw forever — no migration.
- **Phase 99 (OSV-AUD-004) context-bound AEAD:** `ChunkStore::append_chunk`/`read_chunk`
  take a `crypto::ChunkTag` (domain, owner node_id, per-record id, sequence,
  context_bound). New-vault appends regenerate a fresh random per-record id; reads derive
  byte-identical AD from the authenticated index alone. `header.h` gained
  `FLAG_CONTEXT_BOUND_CHUNKS` + the immutable `vault_id` (owner of the Index/MkWrap AD —
  NOT the salt, which `change_password` rotates). The index blob (`index_io`) and
  master-key wrap (`vault.cpp` create/unlock/change_password) seal with the vault_id-
  bound AD, gated on the flag, so legacy vaults stay contextless. `read_thumb_span` now
  takes a `vault::ChunkRef` (see `vault/chunk_ref.h`) so any-thread span readers
  (gallery covers, duplicate scan, migration workers) carry the full decrypt context
  without danging an `IndexNode`. In-place `compact()` still moves ciphertext verbatim —
  offsets stay out of the AD by construction.
