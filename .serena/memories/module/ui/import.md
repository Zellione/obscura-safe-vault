# Module: ui/import — archive import, zip/libarchive, folder scan, volume assembly

Import infrastructure: archive handling, plan building, vault population, import queue, progress tracking.

## Import planning & archive reading
- `folder_scan.*` (Phase 51) — `scan_folder(root) -> vector<ZipEntry>` via
  `std::filesystem::recursive_directory_iterator` with `skip_permission_denied` and symlinks
  skipped (never followed, containment verified). Emits archive-style relative paths as `ZipEntry`,
  bounded by an entry-count limit. Error recovery: permission denied non-fatal, continue scan.
- **Phase 53 recursive + multipart archive stack** (each pure/SDL-free unless noted):
  - `archive_kind.*` — `detect_archive_kind(name,bytes)` (extension proposes, magic confirms) and
    `is_archive_name(name)` (extension only). The split is deliberate: the planner has entry NAMES
    but no bytes, so it classifies permissively and the orchestrator magic-checks once bytes exist,
    demoting a liar to `skipped_unsupported`. `archive_extension_of` is the shared table so the two
    cannot drift.
  - `recursive_import.*` — `walk_archive`, depth-first, backends INJECTED as hooks (so this module
    links neither miniz nor libarchive and the recursion is testable with fakes). A recursive plan
    cannot be a flat placement list — each placement indexes a *different* archive buffer — so it
    emits through hooks as it goes. Depth-first is load-bearing: only the archives on the current
    root->leaf path are live, which is what the live-bytes guard polices. Each archive's own
    meta.json tags the gallery IT produced. `RecursionBudget` = five guards (depth, total expanded,
    live bytes, nested count, expansion ratio), each failing only its BRANCH. Also
    `nested_gallery_name` / `unique_gallery_name`. Refuses unset hooks (a default std::function
    throws, and the project is exception-free).
  - `recursive_hooks.*` — the real backends: miniz for zip/cbz, ArchiveReader for the rest,
    MediaSink for galleries/placement/tags. `create_gallery` treats AlreadyExists as success
    (every plan lists every ancestor, so it is the normal case). The archive password is held in
    a `SecurePassword` = `shared_ptr<crypto::SecureBytes>` (via `make_secure_password`), shared by
    the list/extract closures and `crypto_wipe`'d when the last closure dies — NOT a plain
    std::string, which would leave the plaintext password in freed heap (invariant #2; security
    audit finding, PR #118). Backends still get a `string_view` over the buffer.
  - `recursive_exec.*` — `import_archive_recursive`, what ImportQueue's Zip/Archive tasks call.
    `sink_root` is REQUIRED, not defaulted: DirectVaultSink's base already includes the gallery
    name, StagingSink's does not, and guessing drops or duplicates a whole gallery level.
  - `volume_set.*` — `detect_volume_set` (pure over a sibling listing) + `assembly_for` +
    `concatenate_volumes`. Four styles; two ordering traps: a spanned zip's `.zip` is LAST though
    `.z01` sorts first, and old-style RAR's `.rar` is volume ONE despite sorting after `.r00`. A set
    is contiguous from its OWN minimum (7z starts 001, `split -d` starts 00). Two-volume minimum is
    what stops a lone `photos.zip` reading as a broken set.
  - `volume_import.*` — `summarize_volume_set` (pure; a gap is a REFUSAL, not a warning) +
    `assemble_volume_set` (reads volumes, applies `platform::normalize_user_path`, returns bytes
    for Concatenate/SpannedZipMerge or paths for FileOriented).
  - `spanned_zip.*` — `merge_spanned_zip`: strips the 4-byte spanning marker, rewrites every CD
    entry's disk number + local-header offset to absolute, fixes the EOCD. Disk-0 offsets must
    subtract the stripped marker or the CD enumerates perfectly and every extraction fails. Zip64
    spanned rejected (v1). Bounds-checked throughout.
  - `selectable.*` — `is_selectable` / `selectable_indices`, the ONE rule for what may enter a
    selection (image, video, gallery). Was inline in three places, which is how Ctrl+A and Space
    drift apart.
  - `volume_set_dialog.*` (SDL) — `VolumeSetDialog`: the confirm modal over a `VolumeSetSummary`.
    `Result{Pending,Confirmed,Cancelled}`; Enter/Y confirm, Esc/N cancel. Unlike ConsentDialog it
    has a state where confirming is NOT offered: `!can_import` makes Enter return `Pending`
    WITHOUT closing, so the user can read which volume is missing. Border ACCENT vs DANGER and the
    key hint both derive from `can_import`, so an unimportable set never advertises Enter. Long
    sets elide after 8 rows.
- `zip_plan.*` — pure ZIP placement planner: entries -> galleries to create + file placements +
  skip count. SDL-/miniz-/vault-free, unit-tested. `build_zip_plan` mirrors the archive tree 1:1;
  a dir holding both media and subdirs maps onto a mixed gallery (Phase 46), so there is no
  conflict policy and no user prompt. `ZipDest{NewGallery,Append}`. Phase 53: `ZipPlan::nested`
  collects entries whose NAME claims an archive (routed away from `skipped_unsupported`), and the
  parent gallery is created even for a directory holding only an archive. `build_cbz_plan` never
  populates it — a comic archive is a flat run of pages.
  `is_supported_image_name` + `build_cbz_plan` -> a fixed one-leaf plan (gallery named after the
  archive) of every image entry, videos/other skipped+counted, subfolders flattened (basename
  collisions disambiguated by source dir), natural reading order. `find_meta_entry` — a
  top-level meta.json (ci, files only) excluded by every planner path.
- `zip_encoding.*` — `decode_zip_entry_name`: legacy (non-UTF-8) zip/cbz entry-name decoding.
  When a name lacks the UTF-8 flag (bit 11), decodes via a fixed 128-entry CP437->Unicode table
  (0x80-0xFF; 0x00-0x7F ASCII), unless the raw bytes already parse as valid UTF-8 (passed
  through). Shift_JIS/other double-byte out of scope (imports safely, mis-decoded as CP437).
  Pure, unit-tested; used by zip_import's read_entry_list. `is_valid_utf8(string_view)` is
  public since Phase 72 (also used by ArchiveReader's entry-name choice): conservative —
  rejects overlong encodings, surrogate halves, >U+10FFFF.
- `zip_import.*` — ZIP/CBZ import executor: miniz reader -> mlock'd SecureBytes (one entry at a
  time, no temp file) -> Vault::add_image/add_video by `image::detect_format`.
  import_cbz reuses the per-entry path over build_cbz_plan (`.cbz` -> one
  page gallery, never extracted to disk). Lives in ui/ like export.* (deps vault+image). Hosted
  by GalleryGrid (`Z` key). Optional `ImportProgress*` (atomic total/done/cancel). A top-level
  meta.json seeds the created gallery's tags via `Vault::add_tag`, but the name is NOT applied
  by the importer: `peek_archive_meta` reads meta at file-pick time, GalleryGrid prefills the
  name popup with `meta_gallery_name(meta,stem)` (confirmed popup text is authoritative).
  1 MiB meta cap; malformed meta.json never blocks the import.
- `archive_reader.*` — ArchiveReader: thin wrapper over libarchive's streaming read API
  (`archive_read_open_memory` over an mlock'd buffer), whole-file gated `OSV_VENDORED_ARCHIVE`.
  `open()` does one forward pass building `entries()` (reuses ZipEntry from zip_plan.h,
  format-agnostic); `extract(index,out)` holds a FORWARD STREAM CURSOR (PR #124): the open
  stream stays positioned past the last extracted entry, so ascending extracts (the import
  loop's pattern) share one stream and a full import is a single pass — libarchive has no
  random access, and the old reopen-per-extract was O(n²) decompression (~150x slower than
  zip on a solid 7z). An index behind the cursor transparently reopens; ANY extract failure
  resets the cursor (per-call independence preserved); `stream_opens()` observable pins the
  contract in tests. Reader is now non-copyable/non-movable (destructor frees the handle).
  `MAX_ENTRY_BYTES=4 GiB` bomb guard checked against the declared size before allocating.
  `open_files(paths, passphrase)` (Phase 53) opens an ORDERED multi-volume set via
  `archive_read_open_filenames` — libarchive's RAR multivolume support only works file-oriented,
  because each volume carries its own header and concatenation truncates. Paths are NOT
  normalised here; `normalize_user_path` stays at the boundary where paths enter (volume_import).
  On Windows the wide `archive_read_open_filenames_w` is used (Phase 72) — the narrow variant
  reaches fopen through the ANSI code page and cannot open CJK volume paths.
  **Entry names (Phase 72):** `scan_entries` names each entry via the file-local
  `entry_name_utf8`: the narrow `archive_entry_pathname` IF it is valid UTF-8 (authoritative
  raw bytes for tar), else the wide `archive_entry_pathname_w` converted locale-independently
  through `std::filesystem::path` (the Windows path — libarchive on Win32 always keeps the
  wide form even when the ACP conversion fails), else the raw narrow bytes (legacy tar;
  `sanitize_node_name` repairs downstream). Depends on `platform::init_locale()` having run
  (app/test main): libarchive converts 7z/RAR UTF-16 names through the current locale at
  parse time, and under `"C"` every accessor returns NULL (pre-fix such entries collapsed to
  "unnamed"). Test fixture: `tests/ui/fixtures/cjk_names.7z.uu` — a committed real-7-Zip
  archive (uuencoded), because libarchive's own 7z WRITER converts names through the locale
  and cannot produce CJK names under `"C"` (archive_test_helpers' make_archive documents this).
- `archive_import.*` — import_archive/import_archive_cbz: mirrors zip_import's structure but
  backed by ArchiveReader, covering .7z/.rar/.tar(+.gz/.xz)/.cbr/.cb7/.cbt. Declared
  unconditionally; the .cpp branches internally on OSV_VENDORED_ARCHIVE, returning a graceful
  "not supported" outcome without it, so gallery_grid.cpp needs zero #ifdefs. GalleryGrid's
  `classify_archive_ext()` picks miniz vs libarchive backend + CBZ-style vs mirror/append plan
  purely from the extension.
- `meta_json.*` — pure archive meta.json parser (nlohmann/json, header-only): `parse_meta_json`
  (tolerant, exception-free) -> `ArchiveMeta{title_english,title_japanese,tags}`;
  `meta_gallery_name` (english->japanese->fallback; '/'->'_') + `meta_gallery_tags` (japanese
  title first, searchable; tags failing `tag_has_renderable_text` — CJK-only titles, bracket
  shells — are dropped, PR #148). Unit-tested.

## Background import queue (Phase 50)
- `import_queue.*` — `ImportQueue`: lifecycle managed by App (owns one, destroyed on app shutdown).
  One worker jthread + decode pool (min(hw,4) threads). Per-file pipeline: read source → decode → encrypt → append chunks → stage IndexNode.
  A Files task routes each pick by content, exactly like the archive/folder importers: `image::detect_format`
  hit → `stage_image`, everything else → `stage_video` (its container probe rejects non-video junk → skipped).
  Before PR #171, Files tasks staged EVERY pick as an image — an mp4 picked directly became an Unknown-format
  image node (no thumbnail, no player); such pre-existing nodes need delete + re-import (video_repair skips them).
  Ordering: decode parallel, append+attach strictly in sequence via a resequencer (lookahead cap 8 items/256MiB).
  Methods: `enqueue` (any thread, refuse if stopped), `abort_and_flush` (idempotent), `begin_session` (clears stale state/flags),
  `set_exclusive` (inhibit until released). Worker stops gracefully on Vault::lock().
  **Phase 65:** `maybe_end_batch()` is latched by `batch_ended_` — end-of-batch `enqueue_snapshot()` + `flush()` 
  fires once per busy→idle transition, re-arming when work arrives (reset in `begin_session()`). Previously 
  (Phase 50–64): the snapshot+flush ran unconditionally every idle frame, appending a full index blob and growing 
  the vault at ~795 bytes/second with no user input, unbounded.
  **Phase 51:** `enqueue_folder(vault, folder_path, dest_gallery_path, progress)` enqueues an ImportTaskKind::Folder,
  mirroring `enqueue_files`. Multiple folder picks create multiple tasks (one per folder).
  **Phase 68 (crash resilience):** `run_worker_task` wraps every task's processing in a
  task-boundary try/catch — an escaping exception marks THAT task Failed ("Internal import
  error: <what>") instead of std::terminate (the decode-pool job body is guarded the same way).
  The generic catches are deliberate (accepted S1181/S2738 deviation, commented at the site).
  `mark_task_complete(task_id, result, progress)` now writes the worker's local task copy BACK
  to the queued row — terminal Failed/Cancelled state + error, counts merged via `max()`
  against the live drain-incremented counters (before Phase 68 the copy was discarded and a
  failed archive rendered "✓ Done, 0 imported"). Every Failed task logs ONE
  `platform::log_error("Import", import_failure_log_line(name, reason))` line to
  `<config_dir>/error.log` from the choke point after mark_task_complete (names + ASCII
  reasons only — invariant #5). A corrupt ROOT archive fails the task ("Archive is corrupt or
  unreadable", `RecursiveTally::root_unreadable`) instead of folding into `skipped`; a nested
  unreadable archive stays a per-entry skip. `zip_import.h`'s `zip_entry_size_plausible(comp,
  uncomp)` refuses a zip entry claiming to inflate past deflate's ~1032:1 bound BEFORE an
  mlock'd buffer is sized from the lie. Test seam: `test_only_set_task_hook(q, fn)` (friend)
  runs fn inside the try scope on the worker.
  **Phase 53:** `enqueue_volume_set(volumes, style, stem, dest, gallery_name, kind, password)` — Task gained
  `volumes` / `volume_style` / `volume_stem`. The whole ordered volume list must ride ON the task: the worker
  runs off a task SNAPSHOT, and a snapshot carrying only `source` silently imports the first fragment alone.
  Kind is detected from the set's STEM, never a volume filename — `whole.zip.00` has extension `.00`.
  `process_volume_set_task(task, sink, pw) const` is a separate method (extracted for cognitive complexity):
  assemble → route by `VolumeAssembly` → `merge_spanned_in_place` for spanned zips → the normal recursive import.
- `import_model.*` — pure, SDL-free queue model: `ImportTask` (id, kind, source, dest, gallery_name, optional archive password);
  `ImportTaskKind{Files, Zip, ArchiveZip, Archive7z, ArchiveRar, ArchiveTar, Folder}` (Phase 51);
  `ImportRecord{task, state, progress, error}`; `ImportQueueModel` (FIFO/reorder/cancel/drain).
  `footer_import_summary(records,lane_error)` — formats "Importing X 128/450 · 2 queued" for the footer.
  Unit-tested, no vault dep.
- `MediaSink` (Phase 50) — executor seam for add_image/add_video, unifying legacy `FileOpJob` path (now retired)
  and new queue path. Abstract `struct MediaSink` with `add_image(Vault,...)` and `add_video(Vault,...)` virtuals;
  `DirectVaultSink` calls vault directly (synchronous); `test_only_*` seam for testing.
  **Phase 53:** gained a `tag_gallery(vault, gallery_path, tag)` PURE VIRTUAL — deliberately not a defaulted
  no-op, because that is exactly how `StagingSink` silently dropped every meta.json tag on queued imports
  while the synchronous path kept working. `StagingSink`'s `StagedRecord` gained a `tag` field so tags survive
  until the main-thread drain applies them.
- **Retired:** `ZipImportJob` (entire class), `FileOpJob::start_import` (method); executors (zip_import, archive_import, etc.)
  reused by queue unchanged. GalleryGrid `Z` key now enqueues instead of launching a legacy job.

## Workers (Phase 51)
- `process_folder_task(Vault, task, sink, progress)` — mirror of `process_archive_task`, executes an
  ImportTaskKind::Folder: `scan_folder(task.source)` -> archive-style ZipEntry list -> `build_zip_plan` -> stage each
  file (decrypt-to-memory, no temp file) -> attach gallery tree. Bytes read into mlock'd SecureBytes.
  Main-thread-only invariant: tree untouched by worker (attach/commit only on queue drain).
