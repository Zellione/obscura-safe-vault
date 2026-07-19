# Phase 30 Implementation Plan — PDF Import

**Date:** 2026-07-19  
**Goal:** Import a `.pdf` as a gallery of page images (like CBZ), rendering each page into locked memory only — never a temp file.

---

## Dependency: PDFium Vendoring

**Decision:** Use OlexiyKhokhlov/PDFium CMake fork (verified Jul 2026)
- Actively maintained (commits Jan 2026)
- CMake build system (matches project's codec build pattern)
- Linux & Windows support (project primary platforms)
- BSD-3-Clause license preserved
- CI workflows present (ubuntu-x64, windows-x64)
- Submodules: fast_float (https://github.com/fastfloat/fast_float)

**Build Integration:**
- Add `vendor/pdfium` as git submodule (OlexiyKhokhlov/PDFium fork)
- Create `scripts/build_pdfium.sh` wrapper (model on existing `scripts/build_codecs.sh`)
  - Calls `cmake -B vendor/codecs-prefix-pdfium ... && cmake --build ... --config Release`
  - Installs library into `vendor/codecs-prefix/lib/libpdfium.a` (or `.lib` on Windows)
  - Gated by environment check; silently skips if already built
- Add `link_pdfium()` function to `premake5.lua` (gates on `OSV_VENDORED_PDFIUM` define)
- Update `scripts/setup.{sh,bat}` to call `build_pdfium.sh` (or equivalent Windows CMake build)
- Update `scripts/build_codecs.sh` to optionally build PDFium (or keep separate)

---

## Source Files to Create/Modify

### New Files
1. **`src/media/pdf_render.{h,cpp}`** — PDFium rendering wrapper
   - `class PdfDocument` — RAII wrapper around `FPDF_DOCUMENT`
   - Constructor: load from `std::span<const uint8_t>` (from SecureBytes, never a file path)
   - `page_count() → int`
   - `render_page(int page_num, int dpi, SecureBytes& out_rgba) → bool`
   - Handles error cases: invalid page number, render failure
   - Auto-closes document on destruction
   - Gate entire file behind `#ifdef OSV_VENDORED_PDFIUM`

2. **`src/ui/pdf_import.{h,cpp}`** — PDF import executor & plan builder
   - **Plan builder:** `build_pdf_plan(int page_count, string_view base_gallery, string_view pdf_filename) → ImportPlan`
     - Returns: `{ galleries: [one leaf], placements: [one image per page in order] }`
     - Pure, testable
     - Gallery name: sanitize_node_name(pdf_filename without .pdf extension)
   - **Executor:** `import_pdf(Vault& v, path pdf_path, string_view base_gallery, string_view gallery_name, ImportProgress* progress) → ZipImportOutcome`
     - Read PDF file into `SecureBytes` (mlock'd)
     - Load document via PDFium
     - Create gallery
     - For each page:
       - Render to RGBA at 150 DPI into `SecureBytes` (mlock'd, wiped on scope exit)
       - Dispatch to `add_image` via detect_format (always RGBA, never raw PDF bytes)
       - Tally result
     - Wipe source PDF buffer on function exit (SecureBytes destructor)
     - Errors: malformed PDF, corrupt page render, encrypt-protected → graceful failure with message
   - Reuse `ZipImportOutcome`, `ImportProgress`, `ImportCommon` (shared patterns)

### Modified Files
3. **`src/ui/gallery_grid.cpp`** — Wire up file dialog & route
   - Add `Purpose::Pdf` to the file-dialog filter enum (alongside `Zip`)
   - In import-handler switch: `case Purpose::Pdf: return import_pdf(...)` (distinct from `Z` zip path)
   - Reuse existing destination modal (new/append gallery)

4. **`premake5.lua`** — Link PDFium
   - Add function `link_pdfium()` (mirrors `link_av()`, `link_archive()`)
   - Checks for `vendor/codecs-prefix/lib/libpdfium.a` (Linux) or Windows equivalent
   - Defines `OSV_VENDORED_PDFIUM` only when present
   - Call from main target (app + tests)

5. **`CLAUDE.md`** — Document new dep
   - Add PDFium row to Technology Choices table
   - Note BSD-3-Clause license, OlexiyKhokhlov fork, CMake build, render-only

6. **`docs/VENDORED_DEPS.md`** — Track CVE review
   - Add PDFium table row: version (from git tag), BSD-3-Clause, parses untrusted input (PDFs)
   - Include quarterly CVE review cadence

7. **`README.md`** — Update build prerequisites
   - Note: PDFium requires cmake (already required by other codecs)

8. **Updated via Serena:**
   - `mem:tech_stack` — PDFium version/build/CI cache details
   - `mem:core` — `src/media/pdf_render.{h,cpp}` + `src/ui/pdf_import.{h,cpp}` module descriptions

---

## Tests

### Fixtures
- **`tests/media/fixtures/simple.pdf`** — single-page PDF (100x100 RGBA)
- **`tests/media/fixtures/multi.pdf`** — 3-page PDF (each 200x150)
- **`tests/media/fixtures/corrupt.pdf`** — malformed header (0x00 overwritten)
- **`tests/media/fixtures/encrypted.pdf`** — password-protected PDF (password required)

### Unit Tests (`tests/media/test_pdf_render.cpp`)
1. `PdfDocument_loads_from_memory_buffer` — verifies page count
2. `PdfDocument_renders_page_to_rgba` — render a page, check output is RGBA
3. `PdfDocument_rejects_invalid_page_number` — out-of-bounds page
4. `PdfDocument_rejects_malformed_pdf` — corrupt PDF fails gracefully
5. `PdfDocument_rejects_encrypted_pdf` — password-protected PDF fails gracefully
6. No-fs-write assertion (assert 0 bytes written to temp dirs)

### Integration Tests (`tests/ui/test_pdf_import.cpp`)
1. `build_pdf_plan_creates_one_gallery_per_pdf` — plans 1 gallery + N pages
2. `build_pdf_plan_orders_pages_sequentially` — page 1, 2, 3 in order
3. `build_pdf_plan_sanitizes_pdf_filename` — name goes through sanitize_node_name
4. `import_pdf_fixture_creates_gallery_with_correct_page_count` — multi.pdf → 3-page gallery
5. `import_pdf_fixture_images_have_matching_checksums` — each page has expected content
6. `import_pdf_malformed_fails_gracefully_with_message` — corrupt.pdf → error, no crash
7. `import_pdf_encrypted_fails_gracefully_with_message` — encrypted.pdf → error, no crash
8. `import_pdf_no_fs_write_assertion` — assert no bytes to `/tmp`, `$TMPDIR`, etc.

### Test Command
```bash
scripts/test.sh           # All tests (Debug)
scripts/test.sh --asan    # AddressSanitizer + UBSan + LSan (mandatory for file import)
```

---

## Security Invariants

✅ **No plaintext to disk**  
- PDF file read into `SecureBytes` (mlock'd)
- Each page rendered into temporary `SecureBytes` (mlock'd)
- Auto-wiped via destructor on scope exit
- No temp files, no cache

✅ **Malformed/encrypted PDFs fail gracefully**  
- PdfDocument::render_page returns bool (false on error)
- Errors surfaced via ZipImportOutcome::error message (user-facing modal)
- No crash, no partial import

✅ **Names through sanitize_node_name**  
- Gallery name derived from PDF filename base
- Validated before creating gallery
- Follows Phase 36 rules (no path injection, CJK-safe, etc.)

✅ **No key material logged**  
- PDF file content never logged
- Errors logged only as generic "[PdfImport] render failed: ..."

---

## Implementation Steps (TDD)

1. **Write tests first** (fixtures + unit + integration)
2. **Implement PdfDocument** (`src/media/pdf_render.{h,cpp}`)
   - Load from buffer via PDFium C API
   - Render page to RGBA (call PDFium_Render functions)
   - Error handling for corrupt/invalid/encrypted
3. **Implement pdf_import** (`src/ui/pdf_import.{h,cpp}`)
   - Plan builder (pure, matches cbz_plan pattern)
   - Executor (read → load → render loop → add_image)
4. **Wire up UI** (`src/ui/gallery_grid.cpp`)
   - Add Purpose::Pdf to file dialog
   - Route to import_pdf
5. **Build integration** (`premake5.lua`, `scripts/build_pdfium.sh`)
   - Link PDFium, define `OSV_VENDORED_PDFIUM`
   - Ensure Linux + Windows paths work
6. **Run tests**
   - `scripts/test.sh` — all green
   - `scripts/test.sh --asan` — no leaks/UB
7. **Update docs**
   - CLAUDE.md, VENDORED_DEPS.md, README.md
   - Serena memories (`mem:tech_stack`, `mem:core`)
8. **Update phase status**
   - ROADMAP.md Phase 30: ✅
   - phase-30-import-pdf.md checkboxes: all [x]
9. **Commit & PR**
   - Branch: `phase-30-import-pdf`
   - One commit per logical unit (tests, impl, build, docs)

---

## DPI & Resolution

Target: **150 DPI** (sensible for gallery-sized PDFs)
- A4 page at 150 DPI → 1240 × 1754 pixels (acceptable memory + quality tradeoff)
- Matches typical document preview use case
- Configurable in future if needed (out of scope for Phase 30)

---

## Known Limitations

- **No encrypted PDF support** — password-protected PDFs rejected (out of scope Phase 30)
- **Per-page quality not configurable** — fixed at 150 DPI
- **No text extraction** — page content rendered as images only
- **No XFA forms** — OlexiyKhokhlov fork excludes XFA (simplifies build, reduces attack surface)

---

## Acceptance Criterion

✅ A fixture 3-page PDF imports as one gallery with 3 images (page 1, 2, 3 in order)  
✅ Page checksums match expected content  
✅ A corrupted/encrypted PDF fails gracefully with a user-facing message  
✅ `scripts/test.sh` and `scripts/test.sh --asan` pass (0 leaks, 0 UB)  
✅ No bytes written to disk during import (temp dir assertion)  
✅ CLAUDE.md, VENDORED_DEPS.md, README.md, and Serena memories updated  

---

## CI & Platform Coverage

- **Linux (x86_64)**: Native build via cmake (tested in dev environment)
- **Windows (x86_64)**: CMake generates VS2022 solution (via `bin/premake5.exe vs2022`)
- **macOS**: Dropped from project scope (per CLAUDE.md), not tested

---

## Rollback

If OlexiyKhokhlov/PDFium fork has build issues on CI:
1. Fall back to paulocoutinhox/pdfium-lib (Python make.py wrapper)
2. Create `scripts/build_pdfium_python.sh` (model on FFmpeg's configure wrapper)
3. Document in plan doc, commit, re-run CI
4. Notify owner if neither works (requires relaxing vendoring philosophy)

---

## References

- **Phase 17:** ZIP/CBZ import pattern (zip_plan, zip_import, SecureBytes)
- **Phase 34:** libarchive integration (cmake codec build, link_archive pattern)
- **Phase 25:** Background progress modal (ImportProgress, OpProgress)
- **PDFium docs:** https://pdfium.googlesource.com/pdfium/
- **OlexiyKhokhlov fork:** https://github.com/OlexiyKhokhlov/PDFium
