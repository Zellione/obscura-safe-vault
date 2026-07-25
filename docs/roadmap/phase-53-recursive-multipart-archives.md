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
- [x] `ZipPlan::nested` + `build_zip_plan` routing — archive entries divert to `nested` instead of `skipped_unsupported`; the parent gallery is created even for a directory holding only an archive. 7 tests.
- [x] **Design correction (recorded).** The spec had the planner call `detect_archive_kind`, but `collect_media` sees `ZipEntry{path, is_dir}` — names, no bytes — so it cannot magic-check. Classification is split: `is_archive_name` (extension only) at plan time, `detect_archive_kind` (extension + magic) at extract time, where a liar is demoted to `skipped_unsupported`. Both share one `kArchiveExts` table.
- [x] Naming: `nested_gallery_name` (extension stripped incl. `.tar.gz`, `sanitize_node_name` applied) + `unique_gallery_name` (`base_2`, `base_3`, …). 10 tests.
- [x] CBZ does not recurse — `build_cbz_plan` has its own collection loop and never calls `collect_media`; guarded by a test rather than left to the call graph staying that way.
- [x] `src/ui/recursive_import.*` — `walk_archive`, depth-first, hooks-injected so the recursion is testable without miniz/libarchive. A recursive plan cannot be a flat placement list (each placement indexes a *different* archive buffer), so it emits as it goes.
- [x] `RecursionBudget` — the five guards, each failing only its own branch. 8 tests.
- [x] `src/ui/recursive_hooks.*` — real backends (miniz / ArchiveReader / MediaSink), proven on real nested zips.
- [x] `src/ui/recursive_exec.*` + `ImportQueue` routing — nested import reachable from the actual UI path, with a queue-level test.
- [x] Meta.json applied per nested sub-gallery — each archive tags the gallery IT produced, via a `tag_gallery` hook. Required adding `MediaSink::tag_gallery`, which also fixed queued imports never applying archive tags at all.
- [x] Guards (each soft-fails the offending branch, none abort the whole job, each unit-tested separately):
  - `kMaxArchiveDepth = 16`
  - cumulative expanded-bytes cap (e.g. 10 GiB max across whole recursion)
  - live-bytes cap along current path
  - nested-archive-count cap (e.g. max 1000 discovered)
  - expansion-ratio zip-bomb guard (e.g. 100× threshold)
  - Existing per-entry 4 GiB cap and per-file 512 MiB queue cap still apply.
- [ ] **Partially done:** the parent's password IS passed to every nested archive. But a nested archive needing a *different* password currently tallies as `unreadable`, not `encrypted_skipped` — libarchive's passphrase-failure signal is not yet distinguished at this level.
- [x] Progress: `OpProgress::expanding` + `note_planned` hook; `format_task_progress` is pure and tested (incl. total still 0, where "12/0" would read as a defect).
- [x] Result tallies exist on `RecursiveTally` (nested, not-an-archive, unreadable, depth-capped, budget-stopped, encrypted-skipped).
- [ ] **Not done:** tallies are currently COLLAPSED into `ZipImportOutcome::skipped`, so the Import Status screen shows one number rather than breaking out *why* something was skipped.
- [x] Tests: recursion depth, per-branch guard trips, naming + collision suffixing, meta.json per sub-gallery, CBZ non-recursion, unreadable-nested, cancellation, unset hooks. Real nested zips end-to-end plus a queue-level test. Encrypted-nested skip path is NOT covered (see above).

**3. Multipart archive detection** ✅
- [x] `src/ui/volume_set.h/.cpp` — pure detection over supplied directory listing. `enum VolumeStyle { NumericSuffix, RarPart, RarOld, SpannedZip }`. `detect_volume_set(picked_path, siblings_span)` classifies and orders volumes, detects gaps. Pure function, no filesystem operations inside it.
- [x] Styles confirmed against real tool output, not convention: 7z emits `.7z.001`…`.103` (3-digit, starts at **001**), `split -d` emits `.tar.00`…`.54` (2-digit, starts at **00**), `zip -s` emits `.z01`…+`.zip`, RAR uses `.part1`/`.part01`/`.part0001` — all three padding widths appear in libarchive's own fixtures. Variable digit width folded into NumericSuffix; a set is contiguous from its **own** minimum, since normalising to 1 would invent a gap in every `split -d` set.
- [x] Tests (19): ordering per style incl. the two traps — spanned zip's `.zip` is LAST though `.z01` sorts first (and `.z10` must not sort before `.z02`), and old-style RAR's `.rar` is volume ONE despite sorting after `.r00`. Gap detection, two-volume minimum (what stops a lone `photos.zip`/`scans.rar` reading as a broken set), unrelated sets in one directory, non-volume siblings ignored.

**4. Multipart assembly — NumericSuffix and RarPart / RarOld**
- [x] **NumericSuffix:** `assembly_for` + `concatenate_volumes`, proven end-to-end on a real `split -d` set (detected from a directory listing, joined, read back through miniz byte-for-byte). Fixture must use incompressible data or `split` produces one volume and the test proves nothing.
- [x] **RarPart / RarOld:** `ArchiveReader::open_files(paths, passphrase)` — new overload using libarchive's `archive_read_open_filenames()` for file-oriented multi-volume RAR support (cite `vendor/libarchive/libarchive/archive_read_support_format_rar5.c` lines for `advance_multivolume`, `merge_block`).
- [x] Tests: open and extract from multi-volume RAR4 and RAR5 fixtures (uuencoded in vendored libarchive test corpus) — both verified working. Paths are NOT normalised inside `open_files`; `normalize_user_path` stays at the file-dialog boundary per existing convention, so **task 6's volume picker must normalise**.

**5. Spanned ZIP merger**
- [x] `src/ui/spanned_zip.h/.cpp` — pure `merge_spanned_zip(volumes_span, out_error)` over byte buffers. Concatenate volumes, strip spanning marker if present (`PK\x07\x08` or `PK00`), locate EOCD via bounded backward scan (64 KiB), walk central directory rewriting entry offsets, rewrite EOCD with merged metadata. Every read bounds-checked; malformed input rejected with specific error.
- [x] ZIP64 spanned archives explicitly rejected in v1 — documented limitation.
- [x] Tests: merge classic-ZIP spanning, correct offset rewriting, spanning marker strip, EOCD relocation, malformed input rejection. **Plus a mandatory end-to-end test** — a real `zip -s` set, merged, reopened through miniz, bytes compared. Synthetic-only tests passed while every extraction failed (disk-0 offsets ignored the stripped 4-byte marker); the fixture must use incompressible data or `zip -s` emits one volume and the test proves nothing.

**6. Multipart confirm dialog & integration**
- [x] `[Z]` pick scans siblings and runs `detect_volume_set`; a detected set opens `VolumeSetDialog` before anything is enqueued. A lone `photos.zip` with no siblings stays an ordinary archive. **Not wired to `[I]`** (plain file import) — a split archive is picked as an archive.
- [x] Gaps block import: the dialog refuses to confirm and offers only Esc, with the missing volume named. Verified visually — complete set draws an accent border and offers Enter/Esc, gapped set draws a red border and offers Esc only.
- [x] Picked paths through `platform::normalize_user_path` — applied inside `assemble_volume_set`, the boundary where listing-derived names enter (invariant 6).
- [x] Routed per `VolumeStyle` by the WORKER (`enqueue_volume_set` + `process_archive_task`): Concatenate and SpannedZipMerge produce bytes for the recursive walker; FileOriented uses `import_archive_volumes` (libarchive `open_files`).
- [x] **v1 limitation (owner-approved):** a multi-volume RAR imports FLAT — each volume carries its own header, so there is no single buffer for the bytes-based walker, and archives nested inside a split RAR are not recursed into.
- [x] Tests (11): dialog decision logic incl. a stray Enter after close; the detection→summary→confirm seam over a real listing; and a queue-level import of a real `split -d` set arriving as one gallery. Dialog *rendering* checked by rendering both states offscreen through the real gfx primitives, not by unit test.

**7. Gallery select-all toggle** ✅
- [x] `SelectionModel::select_all(int count)` + `all_selected(int)`. `select_all(0)` leaves the selection alone (not a deselect request); `all_selected(0)` is false, or Ctrl+A on an empty gallery would clear forever.
- [x] `Ctrl+A` in `gallery_grid_handle_shortcut_keys`, checked before the plain-letter switch. Toggling.
- [x] `{"Ctrl+A", "Select all / none"}` in the help popup's Navigate group.
- [x] **Videos made selectable end-to-end (owner-approved scope widening).** The design assumed mass paths already accepted videos; they did not — Space refused them and `export_one_image` rejected non-images. `export_one_media` now dispatches `read_image`/`read_video`; consent wording says "items". A gallery is still refused.
- [x] Pure `ui::is_selectable` / `selectable_indices` — the rule was inline in three places, which is how Ctrl+A and Space drift apart.
- [x] Tests: 9 model + 4 selectable + 3 export (video, mixed, gallery-refused).

**Cross-cutting**
- [x] ROADMAP index row (landed with the planning PR #111).
- [x] `scripts/gen.sh` run after each new module. **Note:** `osv_tests` enumerates `src/ui/*.cpp` individually in `premake5.lua`; a missing entry fails at LINK, not compile.
- [x] `mem:module/ui` updated with the whole Phase 53 stack. `mem:vault_format` deliberately unchanged — **no `INDEX_VERSION` bump**, nothing about the container changed.
- [ ] `mem:ui_spec` — record `Ctrl+A` select-all and the multi-volume confirm dialog.

### Acceptance criterion

A nested archive (ZIP inside ZIP, 7z inside TAR, etc.) is discovered and placed as a
sub-gallery during planning; its metadata (if present) tags the sub-gallery;
encrypted nested archives are skipped with a clear outcome. A multipart archive set
(`.7z.001`, `.part1.rar`, `.z01`) can be imported from any volume in the set; gaps are
detected and shown in a confirm dialog, blocking import. Spanned ZIPs are merged
correctly and imported as flat archives; ZIP64 spanned is rejected with a message. The
`Ctrl+A` shortcut toggles all-selected on the current gallery's direct children,
displayed in the `F1` help. All tests pass under `scripts/test.sh` and `--asan`.

**Status:** 🔜 In progress — every planned task implemented; polish outstanding.

Everything below the UI is implemented and tested: kind detection, nested planning,
the depth-first walker + its five guards, real backends, queue integration, meta.json
at every level, volume-set detection, all three assembly routes, and the spanned-ZIP
merger. 1304 → 1438 tests, green under `scripts/test.sh`, `--asan` and `--tsan`.

**Outstanding:**
- Per-reason skip tallies on the Import Status screen (currently collapsed into one number).
- Distinguishing an encrypted nested archive from an unreadable one.
- An encrypted SPLIT set cannot be prompted for a password: encryption is not
  probeable from a single volume, so that path fails rather than asking.
- `ArchiveReader` re-scans from the start on every `extract()`; recursion multiplies
  that per nesting level for 7z/rar. Unmeasured. Zip is unaffected (miniz seeks).

**Partially visually verified.** The confirm dialog's two states were rendered offscreen
through the real gfx primitives and inspected. The full pick→confirm→import flow has NOT
been exercised in the running app — see
`.claude/skills/running-the-app`: file dialogs come from the XDG portal, i.e. the real
desktop session, so no import flow can be driven headlessly on this box. Needs a native run.

**Bugs found and fixed here that predate this phase** (all in the background-import path,
all invisible at the vault root, which is why they shipped):
- CBZ imported via the queue never created its gallery (Phase 50).
- A folder imported into a sub-gallery landed under `Parent/Parent/…` (Phase 51).
- Queued imports never applied archive `meta.json` tags at all.
