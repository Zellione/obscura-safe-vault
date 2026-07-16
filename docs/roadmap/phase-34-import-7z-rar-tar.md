## Phase 34 — Import 7z, RAR, and TAR archives ✅

**Goal:** Extend the ZIP/CBZ import pipeline (Phases 17 + 24) to also accept
`.7z`, `.rar`, and `.tar` (+ compressed TAR variants: `.tar.gz`,
`.txz`) — and lift the Phase 24 "no CBR/CB7/CBT" exclusion, so those comic
variants import exactly like `.cbz` does today. Same discipline throughout:
extraction into **locked memory only**, never a temp file.

### Tasks
- [x] **Vendor libarchive** (BSD-2-Clause) as a git submodule, cmake-built
  static into `vendor/codecs-prefix/` by `scripts/build_codecs.{sh,bat}`,
  mirroring the libwebp/libheif pattern. Gate the dependent code behind a
  build define (`OSV_VENDORED_ARCHIVE`) so a build without it still
  links and keeps plain ZIP/CBZ working via miniz. libarchive's gzip/LZMA2
  filters (needed for `.tar.gz`/`.txz` and most real-world `.7z` entries)
  pulled in two more vendored submodules, **zlib** and **xz/liblzma**,
  built into the same shared `codecs-prefix/` and discovered by
  libarchive's own `find_package(ZLIB)`/`find_package(LibLZMA)` via
  `CMAKE_PREFIX_PATH`. **bzip2 dropped from scope**: unlike every other
  vendored dep here, upstream bzip2 ships no CMake build (only a legacy
  `Makefile`/`makefile.msc`), so `.tbz2` is not supported — a disclosed
  trim, not an oversight.
- [x] **`ArchiveReader` abstraction** (`src/ui/archive_reader.{h,cpp}`)
  wrapping libarchive's streaming read API
  (`archive_read_open_memory` over an mlock'd buffer →
  `archive_read_next_header`/`archive_read_data`) so the existing
  `build_zip_plan`/`build_cbz_plan` entry list and the per-entry
  decompress-into-`SecureBytes` executor loop become format-agnostic.
  **The miniz path is untouched** for `.zip`/`.cbz` (no behaviour change,
  no regression risk to the proven fast path); the new backend only
  engages for `.7z`/`.rar`/`.tar`(+variants)/`.cbr`/`.cb7`/`.cbt`.
  `extract()` re-opens and re-scans the stream from the start on each call
  (libarchive has no random-access API) rather than holding every
  decompressed entry in memory at once — O(n) per extract, accepted as
  fine for typical gallery-sized archives.
- [x] **UI wiring** — extended the `Z` import file-dialog filter to the new
  extensions; `.cbr`/`.cb7`/`.cbt` route through the existing one-leaf CBZ
  plan, `.7z`/`.rar`/`.tar` route through the existing mirror/append ZIP
  plan — identical UX (name popup, Flatten/Skip mixed-folder resolution,
  Phase 25 background-progress modal) to today's ZIP/CBZ import. Routing
  is a pure extension classifier (`classify_archive_ext`) in
  `gallery_grid.cpp`; `archive_import.{h,cpp}` is always declared and
  linked (no `#ifdef` at the call site) and returns a graceful
  "not supported" outcome on a build without `OSV_VENDORED_ARCHIVE`,
  mirroring `ui::VideoPlayback`'s non-AV fallback pattern.
- [x] Updated `CLAUDE.md` (tech table + module layout) / `docs/VENDORED_DEPS.md`
  + `mem:tech_stack` / `mem:core`.
- [x] `tests/` — fixture `.7z`, `.tar`, `.tar.gz` archives import with
  matching per-entry checksums via the shared planner tests (`.txz`
  covered at the `ArchiveReader` level); `.cbt`/`.cb7`-style fixtures
  import as one gallery in natural reading order, like today's CBZ tests;
  a malformed/truncated archive is rejected without crashing; a
  **no-fs-write assertion** covers the new backend too.
  **RAR fixture gap (disclosed):** libarchive has no RAR *writer* (the
  format is proprietary; only decode is implemented), so unlike every
  other format here a fixture can't be synthesized at test time. Reused
  libarchive's own tiny test-corpus fixture instead
  (`vendor/libarchive/libarchive/test/test_read_format_rar.rar.uu`,
  BSD-2-Clause — same license as the vendored library), decoded from
  uuencoding into `tests/ui/fixtures/test_read_format_rar.rar`, proving
  byte-exact open/list/extract through the real RAR4 decoder
  (`archive_reader_opens_rar_and_lists_entries`,
  `archive_reader_extracts_rar_entry_bytes`). That fixture's content is
  plain text/symlinks/dirs, not images, so it doesn't exercise the
  image-checksum *import* path — `.rar`/`.cbr` import goes through the
  exact same `archive_read_support_format_all()` code path already
  proven correct for `.7z`/`.tar` (zero RAR-specific code exists to
  diverge), just not import-level fixture-verified. No `rar`/`unrar` CLI
  was available in the dev sandbox to generate an image-bearing archive.
- [x] **Idempotency bug fix in `build_codecs.sh`** (found while building
  the new deps): the "already installed" skip check used
  `find lib -name "*${name}*"`, which also matches leftover
  pkgconfig/cmake-config files from a prior partial install (written
  before the `.a`, so a build that fails mid-compile still leaves them
  behind) — this silently skipped a real rebuild and left libarchive
  linked against whatever zlib/liblzma the host system happened to
  provide instead of the vendored static libs. Fixed to check for the
  actual `lib${name}.a` file.

**Out of scope (YAGNI):** writing/creating any of these archive formats;
nested archives; multi-volume/split RAR (`.r00`, `.partN.rar`); password-
protected archives (Phase 35); `.tbz2` (no CMake build for bzip2 upstream).

### Acceptance criterion
Fixture `.7z`, `.tar`(`.gz`), `.cbt`, and `.cb7`-style archives each import
correctly (matching checksums, correct gallery structure) through the same
import UX as ZIP/CBZ today; malformed archives fail gracefully; a test
asserts nothing is written to disk during import for any of the new formats.
`.rar`/`.cbr` are wired through the identical code path and fixture-verified
at the reader (open/list/extract) level; import-level checksum coverage for
`.rar` specifically is a disclosed gap (see Tasks above) pending a way to
generate an image-bearing RAR fixture.

**Status:** ✅ Done. Built and tested on Linux (Debug + `--asan`, 0 leaks/UB).
**Windows/macOS build-script changes (`build_codecs.bat`, premake Windows
static-lib names) are unverified in this environment** — this repo's
established pattern for a new vendored codec (see FFmpeg-on-Windows,
Phase 15/16) is to land the cross-platform plumbing in the PR and iterate
via CI on any platform-specific breakage.
