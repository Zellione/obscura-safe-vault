## Phase 17 — Import ZIP archives ✅

**Goal:** Import a `.zip` of photos/videos into the vault in one action — either
as a **new gallery subtree** or **appended** to an existing gallery —
decompressing **into locked memory only**, never to a temp file (invariant #1
holds).

### Tasks
- [x] **Vendor miniz** — add `vendor/miniz` git submodule (single-file, public-domain/MIT); compiled directly by premake like monocypher/stb (no system zlib dependency). Update `setup.{sh,bat}` submodule init + `premake5.lua`.
- [x] `src/ui/zip_import.{h,cpp}` — walk the archive's central directory, build a planned gallery tree from entry paths (via the pure `src/ui/zip_plan.*`), and import each entry. Decompress **one entry at a time** into an mlock'd `SecureBytes`; dispatch by `image::detect_format` (image → `Vault::add_image`, else `Vault::add_video`, which validates the container); wiped on scope exit. **No entry is ever extracted to disk.** (Lives in `src/ui/` like `export.*`: it depends on vault + image, so placing it in `vault/` would invert the `image → vault` dependency.)
- [x] **Contents = all supported media** — import every entry whose format is a supported image (JPEG/PNG/GIF/BMP/TGA/HDR/WebP/HEIC/AVIF) or video (H.264/H.265 in mov/mp4/m4v/mkv/webm); skip everything else silently, reporting a skipped-count in a post-import summary.
- [x] **Folder mapping (mirror)** — recreate the zip's directory hierarchy as nested galleries (a flat zip → one gallery). A directory that **mixes media files and subfolders** maps directly onto a mixed gallery. *(Superseded: this originally prompted the user to flatten or skip such a directory, because the pre-Phase-46 leaf invariant made it unrepresentable. Phase 46 removed that invariant, and with it the prompt and the `ZipConflictPolicy` machinery.)*
- [x] **Two destinations** — *Create new gallery* (preserves the mirrored subtree) or *Append to existing gallery*. **Append flattens:** it only adds the archive's media (ignoring subfolders) into the chosen leaf gallery. Filename collisions reuse `add_image`'s existing handling.
- [x] **UI** — a zip-import entry point from the gallery grid (file dialog over `SDL_ShowOpenFileDialog` with a `.zip` filter, reusing the `platform::file_dialog` async pattern), destination/mode choice, and a post-import summary (imported / skipped counts).
- [x] Update `CLAUDE.md` (miniz in the tech table, `src/ui/zip_plan.*` + `zip_import.*` in the module layout) + `mem:tech_stack`/`mem:core`.
- [x] `tests/` — a fixture `.zip` imports as a new gallery with the mirrored tree and matching per-file checksums; append-flatten adds only media into a leaf; unsupported entries are skipped + counted; a malformed/truncated zip is rejected without crashing (extend the fuzz mindset); a mixed-folder zip triggers the resolution path; a **no-fs-write assertion** proves nothing is extracted to disk during import.

**Out of scope (YAGNI):** password-protected/encrypted zips, zip *export*, other
archive formats (tar/7z).

### Acceptance criterion
Importing a fixture `.zip` as a new gallery reproduces its folder tree as nested
galleries with every supported file's checksum matching the original; append
mode adds only (flattened) media into the chosen leaf; unsupported entries are
skipped and reported; a test asserts **no decrypted or archive bytes are written
to disk** during import.

**Status:** ✅ 424 tests pass under `scripts/test.sh` and `--asan` (11 new: 5 zip-plan + 5 zip-import + 1 miniz linkage). miniz git submodule pinned to master commit `e78dfd2` (modern split-source build: `miniz.c`/`miniz_tdef.c`/`miniz_tinfl.c`/`miniz_zip.c`), built with `MINIZ_NO_ZLIB_COMPATIBLE_NAMES` to avoid clashing with libz linked by avformat; compiled by premake like monocypher/stb. Shim header `vendor/miniz-shim/miniz_export.h` keeps the submodule pristine. Archive entries decompress one-at-a-time into mlock'd `SecureBytes` → dispatched to `Vault::add_image`/`add_video` by `image::detect_format` → wiped on scope exit; **zero bytes ever written to disk** (invariant #1 upheld). The executor + pure planner live in `src/ui/` (`zip_import.*`, `zip_plan.*`) like `export.*`, since they depend on vault + image. Folder tree mirrored as nested galleries; mixed-folder directories (media + subdirs) trigger a Flatten/Skip modal; Append mode flattens all media into the current leaf gallery; skipped entries (unsupported format, mixed-folder skip) counted and reported post-import.
