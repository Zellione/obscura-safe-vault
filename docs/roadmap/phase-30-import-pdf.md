## Phase 30 — Import PDF as a gallery of pages 🔜

**Goal:** Import a `.pdf` as a gallery of page images (like CBZ), rendering each
page **into locked memory only** — never a temp file (invariant #1 holds).

### Tasks
- [ ] **Vendor PDFium** — add **PDFium** (BSD-3-Clause, permissive — fits the vendored-static model) as a git submodule built into `vendor/codecs-prefix/` by `scripts/build_codecs.{sh,bat}` (render-only). Gate the dependent code behind a build define (`OSV_VENDORED_PDFIUM`), mirroring `OSV_VENDORED_AV`, so a non-PDF build still links.
- [ ] **Render pipeline** — read the picked file into an mlock'd buffer, load the document from memory, render each page to an RGBA bitmap at a sensible target resolution, and feed the bitmap into the existing `add_image` / thumbnail path. **No page bytes touch disk.**
- [ ] **Plan/executor** — a PDF import plan mirroring `build_cbz_plan`: one leaf gallery named after the file, one image per page in page order, reusing the import-summary UX and the Phase 25 background-progress modal.
- [ ] **UI** — the file dialog accepts `.pdf` (its own `Purpose`), routed to the PDF importer (distinct from the `Z` zip/cbz path).
- [ ] Update `CLAUDE.md` / README (new vendored lib + any build prerequisite) + `mem:tech_stack`.
- [ ] `tests/` — a small fixture `.pdf` imports as a gallery with the correct page count and a **no-fs-write** assertion; a malformed / password-protected PDF is rejected without crashing.

**Out of scope (YAGNI):** extracting embedded images verbatim; text/searchable-PDF handling; per-page DPI/quality UI; PDF export; non-PDF “more formats” (revisit per-format as needed).

### Acceptance criterion
A fixture `.pdf` imports as one gallery of page images in page order, asserted to
write nothing to disk; a corrupt or encrypted PDF fails gracefully with a message.

**Status:** 🔜 Planned.
