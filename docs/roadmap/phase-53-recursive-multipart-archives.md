## Phase 53 — Recursive & multipart archive import, gallery select-all 🔜

**Goal:** Enable browsing and importing arbitrarily nested archive trees, handle
split/multipart archives across all formats, and add a gallery-wide select-all
shortcut for faster mass operations.

Full design, including detailed tradeoffs and threat-model rationale:
[`docs/superpowers/specs/2026-07-25-phase53-recursive-multipart-archives-design.md`](../superpowers/specs/2026-07-25-phase53-recursive-multipart-archives-design.md).

### Tasks

**1. Archive kind detection**
- [x] `src/ui/archive_kind.h/.cpp` — pure `enum ArchiveKind` + `detect_archive_kind(filename, bytes)`. Extension proposes, magic bytes confirm (`PK\x03\x04` for ZIP, `7z\xBC\xAF\x27\x1C`, RAR markers, `ustar` at 257, gzip `\x1F\x8B`). A non-matching magic is skipped silently, never an error. SDL-free, fully unit-testable.
- [x] Tests: magic-byte matching for each kind, extension/magic mismatch handling, edge cases (truncated headers, file too small). 16 tests in `tests/ui/test_archive_kind.cpp`.
- [x] **Note for later tasks:** `osv_tests` lists `src/ui/*.cpp` **individually** in `premake5.lua` (the `osv` app project globs `src/**.cpp`, so only the test project needs it). Every new `src/ui/` module in this phase must be added there or it fails at link, not compile.

**2. Recursive archive planning**
- [ ] `src/ui/recursive_import.h/.cpp` — depth-first orchestrator with work stack. Plans a top-level archive, discovers nested archives during planning, queues them depth-first. `ZipPlan::nested` holds discovered archives; `build_zip_plan` entry classification route non-media archives to `nested` instead of `skipped_unsupported`.
- [ ] Naming: nested archive `foo.cbz` → sub-gallery `foo` (extension stripped, `vault::sanitize_node_name` applied, collision-suffixed). Meta.json applies per sub-gallery.
- [ ] CBZ does not recurse — confirmed in the implementation by explicit guard.
- [ ] Guards (each soft-fails the offending branch, none abort the whole job, each unit-tested separately):
  - `kMaxArchiveDepth = 16`
  - cumulative expanded-bytes cap (e.g. 10 GiB max across whole recursion)
  - live-bytes cap along current path
  - nested-archive-count cap (e.g. max 1000 discovered)
  - expansion-ratio zip-bomb guard (e.g. 100× threshold)
  - Existing per-entry 4 GiB cap and per-file 512 MiB queue cap still apply.
- [ ] Encrypted nested archives: try parent's password; if it fails, skip and tally `encrypted_skipped_count`.
- [ ] Progress: `OpProgress::expanding` bool flag. While discovering: `84 / 210+ (expanding…)`. Once done: `84 / 210` (drop `+`). Accepted tradeoff: percentage can move backwards on large nested archives.
- [ ] Result tallies: `nested_archive_count`, `encrypted_skipped_count`, `depth_capped_count`, `size_exceeded_count`, `nested_archive_count_exceeded`. Surfaced on Import Status screen and footer summary.
- [ ] Tests: depth-first planning order, guard activation per branch, total grows as discovery proceeds, naming collision-suffixing, meta.json per sub-gallery, CBZ leaf behavior, encrypted-archive skip path, progress-flag toggles.

**3. Multipart archive detection**
- [ ] `src/ui/volume_set.h/.cpp` — pure detection over supplied directory listing. `enum VolumeStyle { NumericSuffix, RarPart, RarOld, SpannedZip }`. `detect_volume_set(picked_path, siblings_span)` classifies and orders volumes, detects gaps. Pure function, no filesystem operations inside it.
- [ ] Styles: `.7z.001`/`.zip.001`/`.tar.001` (NumericSuffix), `.part1.rar`/`.part2.rar` (RarPart), `.rar`/`.r00`/`.r01` (RarOld), `.z01`/`.z02`/`.zip` (SpannedZip).
- [ ] Tests: correct ordering per style, gap detection, mismatch-extension rejection, edge cases (single volume not a set, partial sets).

**4. Multipart assembly — NumericSuffix and RarPart / RarOld**
- [ ] **NumericSuffix:** concatenate volumes in order into one buffer, feed existing `archive_read_open_memory` path. Verified working for 7z, zip, tar experimentally.
- [ ] **RarPart / RarOld:** `ArchiveReader::open_files(paths, passphrase)` — new overload using libarchive's `archive_read_open_filenames()` for file-oriented multi-volume RAR support (cite `vendor/libarchive/libarchive/archive_read_support_format_rar5.c` lines for `advance_multivolume`, `merge_block`).
- [ ] Tests: open and extract from multi-volume RAR4 and RAR5 fixtures (uuencoded in vendored libarchive test corpus).

**5. Spanned ZIP merger**
- [ ] `src/ui/spanned_zip.h/.cpp` — pure `merge_spanned_zip(volumes_span, out_error)` over byte buffers. Concatenate volumes, strip spanning marker if present (`PK\x07\x08` or `PK00`), locate EOCD via bounded backward scan (64 KiB), walk central directory rewriting entry offsets, rewrite EOCD with merged metadata. Every read bounds-checked; malformed input rejected with specific error.
- [ ] ZIP64 spanned archives explicitly rejected in v1 — documented limitation.
- [ ] Tests: merge classic-ZIP spanning, correct offset rewriting, spanning marker strip, EOCD relocation, malformed input rejection (truncated headers, missing EOCD, invalid offsets), fixtures generated with `zip -s`.

**6. Multipart confirm dialog & integration**
- [ ] Gallery grid scan for volume sets on any file pick (`[Z]` archive or `[I]` file dialog). If a set detected, show confirm dialog listing volumes, total size, and gaps (flagged in error red). Gaps block import.
- [ ] Picked paths through `platform::normalize_user_path` (invariant 6).
- [ ] Route multi-volume archives to the appropriate assembly backend per `VolumeStyle`.
- [ ] Tests: volume set discovery on file pick, confirm dialog rendering, gap blocking, wrong-volume rejection.

**7. Gallery select-all toggle**
- [ ] `SelectionModel::select_all(int count)` — select indices 0..count-1. Optional: `select_none()` (could reuse `clear()`).
- [ ] `GalleryGrid::handle_key_down()` — catch `Ctrl+A`, call `toggle_select_all()`. Semantics: if all visible direct children already selected, clear them; otherwise, select all. Applies to direct children only (media and sub-gallery tiles).
- [ ] Add `{ "Ctrl+A", "Select all / none" }` to `GalleryGrid::help_groups()`'s "Navigate" group.
- [ ] Tests: toggle when empty, toggle when partial, toggle when full, verify direct-only (nested children not selected).

**Cross-cutting**
- [ ] Update `ROADMAP.md` index row, adding Phase 53 in numeric sequence.
- [ ] `scripts/gen.sh` after adding source files (archive_kind, recursive_import, volume_set, spanned_zip, and any others).
- [ ] Update Serena memories: `mem:module/ui` (new modules + extended selection_model/gallery_grid), `mem:ui_spec` (select-all keybinding, multipart confirm dialog), `mem:tech_stack` if any build config changes.

### Acceptance criterion

A nested archive (ZIP inside ZIP, 7z inside TAR, etc.) is discovered and placed as a
sub-gallery during planning; its metadata (if present) tags the sub-gallery;
encrypted nested archives are skipped with a clear outcome. A multipart archive set
(`.7z.001`, `.part1.rar`, `.z01`) can be imported from any volume in the set; gaps are
detected and shown in a confirm dialog, blocking import. Spanned ZIPs are merged
correctly and imported as flat archives; ZIP64 spanned is rejected with a message. The
`Ctrl+A` shortcut toggles all-selected on the current gallery's direct children,
displayed in the `F1` help. All tests pass under `scripts/test.sh` and `--asan`.

**Status:** ⬜ Not started
