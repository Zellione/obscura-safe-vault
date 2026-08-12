# Module: ui/jobs — background jobs, worker threads, scanning

File operations, duplicate scanning, and migration jobs running on background threads.

## Background jobs (mirror each other; each owns the vault's single-thread file handle)
- `zip_import_job.*` — ZipImportJob runs import_cbz/import_zip (start_cbz/start_zip, shared
  launch()) on a bg `std::thread`; start_archive/start_archive_cbz wrap the same launch() for
  the libarchive path. Contract: while active() the worker owns the vault handle, so GalleryGrid
  must NOT touch the vault (update()/render()/handle_event() short-circuit — no thumbnail
  reads/listing); it polls total()/done() + take_outcome() (joins) + draws a progress modal,
  Esc -> cancel(). poll_import_job keeps the naming state active across a password round-trip
  (encrypted zip/cbz) and clears it on a terminal outcome.
  `Screen::blocks_idle_lock()` (default false; GalleryGrid returns import_job_.active()) stops
  App::maybe_auto_lock wiping the key mid-write. Unit-tested (poll-to-completion harness).
- `file_op_job.*` — FileOpJob runs export / delete / move-copy on a bg worker (same contract).
  start_export/start_delete/start_transfer_images/start_transfer_gallery/
  start_transfer_galleries/start_combine -> `FileOpOutcome{ok,cancelled,done,failed,status,...}`.
  **Phase 76:** `start_combine` takes a `vault::TransferMode` before `label` and forwards it to
  `combine_galleries`; `combine_outcome` verbs follow the mode ("copied"/"moved").
  **Phase 68:** `start_transfer_collection(src, groups, gallery_paths, dst, dst_target, mode,
  label)` runs per-parent `transfer_images` loops then `transfer_galleries`, tallies merged
  (worker body is the named `run_collection_transfer` helper);
  `start_transfer_media_grouped` forwards to it with no galleries. A cancel between
  groups/phases is a clean partial. GalleryGrid owns one for export+delete; TransferDialog
  owns one (Running stage); CollectionBatchOps owns one per collection screen. ImageViewer's
  single-image export stays synchronous. The GalleryGrid gate
  (vault_busy/poll_file_job/handle_job_input) is free friends. Unit-tested.
- `dup_scan.*` (Phase 61) — duplicate-scan worker. `ui::collect_scan_items(vault)` runs on the
  MAIN thread (the index tree is main-thread-only) and snapshots every media node: path, type,
  byte size, thumb/poster span. `DupScanJob` then runs on a bg thread over the snapshot and
  only ever calls the thread-safe `vault::read_thumb_span` (generic chunk-span decryptor —
  data chunks, video chunks, thumbnails alike) — NEVER the tree. Exact pass (images+videos):
  group by (type,size) from metadata, then incremental `crypto_blake2b_*` over the full
  plaintext decrypted into mlock'd `SecureBytes`, wiped immediately — only size-colliding
  files are ever decrypted. Optional perceptual pass (images only, not already
  exact-grouped): dHash over the stored thumbnail. A `Locked` read result ends the scan
  gracefully (manual lock cancels the worker before key wipe); undecodable files are skipped
  and counted; cancel flag checked between items; progress/results polled from the main
  thread. Hashes are session-lifetime heap data — never persisted, never logged.
  **Phase 64:** `refresh_review_members(v, review)` — MAIN-THREAD-only
  post-apply re-resolution: re-reads every remaining member's
  bytes/data_spans/thumb span via `resolve_node` through the file-local
  `refresh_member` helper (drops vanished / gallery / type-changed members,
  counts drops; `DupReview::refresh_members` then drops groups < 2). Needed
  because `remove_media_batch` → `auto_reclaim_space()` can compact() and
  relocate chunks on Windows.
  **Phase 62 — perceptual video pass** (same "Exact + visually similar" mode, no new UI):
  three-stage funnel in `video_perceptual_pass` — duration gate from the snapshot's
  `duration_us` (±2 %, 500 ms floor, zero I/O), poster dHash prefilter (stored first-frame
  JPEG, Hamming ≤ `DUP_VID_POSTER_MAX_BITS`=10, missing poster = plausible), then sampled
  frames for videos with ≥ 1 plausible partner. Frame sigs stay index-aligned with the
  candidate list (parallel vectors — the Phase 61 mapping-bug lesson). Groups emit as
  `DupGroup::Kind::SimilarVideo`; non-FFmpeg builds accept the duration+poster verdict.
- `dup_video_sig.*` (Phase 62) — `compute_video_frame_sig(DupStreamRead, total_size,
  VideoSig&)`: software-decodes one frame at each `DUP_VID_FRAME_POSITIONS` fraction
  (10–90 %) of the timeline through its own callback AVIO (mirrors `media::ChunkAvio`'s
  callbacks) and dHashes each (96×96 RGB24 via swscale). `OSV_VENDORED_AV`-guarded; the
  non-AV stub returns false with `frame_valid` 0. The worker backs `DupStreamRead` with a
  chunk-caching `read_thumb_span` reader (`VideoStream` in dup_scan.cpp — `VideoSource`
  borrows the main-thread `read_fp_` and is FORBIDDEN on the worker). Gotcha fixed here:
  after the demux-EOF flush, `av_packet_unref` resets `stream_index` to 0, so the stale
  packet must never be sent post-flush (`decode_until` flushes exactly once, then drains).
  Listed explicitly in osv_tests' premake5.lua files{}. Fixtures: same 2 s `testsrc2` clip
  pre-encoded H.264/MP4 + VP9/WebM in `scripts/gen_media_fixtures.sh` (lavfi `gradients`
  randomizes colors per invocation — do not use it for "same content" fixtures).
- `migration_job.*` (Phase 65) — one-time blocking vault upgrade pass. `MigrationJob` follows
  the same contract as `FileOpJob`: exclusive vault ownership while `active()`, main-thread
  polls progress and draws a modal, Esc -> cancel(). Unlike import, there is no staging dance
  because blocking ops have no concurrent browsing — the job owns the vault exclusively.
  **Architecture:** One coordinator thread owns the index tree and all writes to `fp_` (guarded
  by `write_mutex_`); a pool of `max(1, hardware_concurrency()-1)` workers decrypt → probe/sniff →
  encode poster. Results read through `vault::read_thumb_span` (the any-thread-safe path). The
  queue is bounded at ~`workers * 2` to stay within the 256 MiB `mlock`'d budget (decoded
  frames + encoded posters live in mlock'd `SecureBytes`). One `commit_index()` at the end,
  then watermark write, then `compact(&progress)` as a third phase if `wasted_bytes() >=
  AUTO_COMPACT_MIN_WASTE` (floor-only gate, no ratio term). Cancel commits applied work but
  does NOT stamp the watermark, so the pass re-runs at the next unlock. Crash mid-pass leaves
  the vault as it was; orphaned poster chunks are dead ciphertext reclaimed by compact.
  **Phase 75 thumb arm:** `Item` gained `Kind{VideoProbe, ImageAnimated, ImageThumb, VideoPoster}`
  + a `thumbs_stale` flag; `run()` computes `thumbs_stale = vault_settings(v).migrated_thumb_side
  < image::THUMB_MAX_SIDE` and passes it to `collect()`, whose arms MIRROR `scan_migration`'s
  (one item per image: ImageThumb — which also sniffs the animated flag — when stale + has a
  thumb, else ImageAnimated; known-codec videos → VideoPoster when stale; Unknown-codec stays
  VideoProbe). `process()` decodes the original → `make_thumbnail(THUMB_MAX_SIDE)` (images) or
  re-probes for a 512 poster (videos); a failed encode/probe is a SKIP (ok=true, empty thumb),
  not a failure. `apply_one()` (coordinator-only) routes to `vault::apply_image_thumb` /
  `apply_video_poster`; the VideoProbe arm ALSO replaces a pre-existing poster via
  `apply_video_poster` when resolved + thumbs_stale (apply_video_probe fills only empty spans).
  `MigrationOutcome` gained `thumbs_fixed`. Test hooks `test_only_force_video_codec_unknown` /
  `test_only_force_image_animated_unknown` (vault.h friends, established test_only_force_*
  convention) let tests fabricate legacy states.
  **Phase 77 fix:** all four `apply_*` calls in `apply_one()`'s helpers now pass `sync=false` (a
  whole-vault thumb regen was fsync'ing once per item — see module/vault); durability instead
  rides `commit_migration()`'s own final sync, which flushes every deferred append made since
  the run started. Also: the progress modal's phase→label mapping moved out of
  `app.cpp::draw_migration_progress` into a pure `ui::migration_progress_text(MigrationPhase,
  done, total) -> MigrationProgressText{title, count_line}` (migration_job.h/.cpp) — `Done` now
  gets its own "Finishing…" label instead of falling into the `Idle`/`Scanning` default of
  "Preparing…", which is what made a finished run's last frame(s) read as a hang.
  **Phase 79:** `abort_and_join()` — cancel + join + deactivate without a `take_outcome()`
  poll (outcome discarded); used by `App::shutdown()` before vault teardown and by the
  destructor (a bare jthread join would block, uncancelled, for the whole pass). The idle
  auto-lock is suppressed while the job is active (see module/app) — it used to fire
  mid-upgrade and tear the vault down under the coordinator.
