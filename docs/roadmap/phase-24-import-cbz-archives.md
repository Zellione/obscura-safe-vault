## Phase 24 — Import CBZ archives ✅

**Goal:** Import a `.cbz` comic archive as a single gallery of pages, reusing the
Phase 17 miniz/ZIP pipeline — decompressing **into locked memory only**, never to
a temp file (invariant #1 holds).

### Tasks
- [x] **Natural-order sort** — a pure filename comparator (`src/ui/natural_sort.{h,cpp}`) so pages order `1 < 2 < 10` rather than lexicographically. Unit-tested.
- [x] **CBZ plan** — a fixed plan distinct from the Phase 17 mirrored-tree plan: **one leaf gallery** named after the archive (sans `.cbz`), containing every supported **image** entry (videos/other formats skipped + counted), **flattening** any internal subfolders, ordered by the natural-order sort over the full entry path.
- [x] **Executor reuse** — reuse the Phase 17 miniz central-directory reader and the per-entry **decompress-one-at-a-time into mlock'd `SecureBytes` → `Vault::add_image` → wipe on scope exit** path. **No entry is ever extracted to disk.**
- [x] **UI** — extend the existing `Z` zip-import file dialog filter to also accept `.cbz`; a picked `.cbz` routes to the CBZ plan (the `.zip` path is unchanged), with the same post-import summary (imported / skipped counts).
- [x] Update `CLAUDE.md` (CBZ handled by the miniz/zip path; new natural-sort unit) + `mem:core`.
- [x] `tests/` — a fixture `.cbz` imports as one gallery named after the file with pages in natural order and matching per-page checksums; non-image entries are skipped + counted; internal subfolders are flattened; a malformed/truncated `.cbz` is rejected without crashing; a **no-fs-write assertion** proves nothing is extracted to disk during import.

**Out of scope (YAGNI):** CBR/CB7/CBT (RAR/7z/tar variants), nested archives, per-page reordering UI.

### Acceptance criterion
Importing a fixture `.cbz` produces a single gallery named after the file whose
pages are in natural reading order with checksums matching the originals;
unsupported entries are skipped and reported; a test asserts **no decrypted or
archive bytes are written to disk** during import.

**Status:** ✅ 553/553 tests pass (`scripts/test.sh`); `scripts/test.sh --asan` clean.
New pure `ui::natural_sort` (digit runs by value, letters case-insensitive) orders
pages; `ui::build_cbz_plan` emits one leaf gallery (named after the archive) of every
image entry, videos/other skipped+counted, subfolders flattened with basename
collisions disambiguated by source dir, in natural reading order. `ui::import_cbz`
reuses the Phase 17 miniz → mlock'd `SecureBytes` → `add_image` path verbatim — no
disk extraction (asserted by a directory-count test). The `Z` file dialog filter now
accepts `zip;cbz`; `GalleryGrid` routes a picked `.cbz` to a fixed one-leaf import
(prompt prefilled with the archive stem) while the `.zip` mirror/append flow is
unchanged.
