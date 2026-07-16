## Phase 10 — Export (selective, hard-gated) ✅

**Goal:** Let the user deliberately extract decrypted images out of the vault to
ordinary files on disk, with explicit per-export consent. This is the **one
feature that intentionally breaks security invariant #1** ("no plaintext to
disk"); it is gated and documented as a deliberate deviation, never a silent or
bulk operation.

### Tasks
- [x] **Selection model** — multi-select in the gallery grid (`Space` toggles, `Esc` clears), plus an "export this image" action from the viewer. Pure, headlessly-tested selection state (no SDL).
- [x] **Consent dialog** — a modal confirmation widget that names the danger explicitly ("Exported files are written **decrypted** to disk, outside the vault's protection") and requires an explicit confirm; cancel/deny is the default focus. Reuse `gfx::Renderer` round-rect/panel primitives.
- [x] `src/platform/folder_dialog.{h,cpp}` — async destination-folder picker over `SDL_ShowOpenFolderDialog`, mirroring the existing `file_dialog` mutex-guarded result pattern.
- [x] **Export writer** — for each selected image: decrypt the **original stored bytes** into an mlock'd `SecureBytes`, write them verbatim to `dest/<original_filename>`, `crypto_wipe` the buffer immediately after the write. Thumbnails are never exported. Name-collision handling appends ` (n)` rather than overwriting.
- [x] **No bulk-tree export** — only the current explicit selection (or a single viewer image) is ever written; there is no "export entire vault" path.
- [x] Update `CLAUDE.md`: record export as a documented, gated deviation from invariant #1; add `src/platform/folder_dialog.*` and the export module to the module layout.
- [x] `tests/` — exported files are byte-identical to the originally-imported bytes; collision suffixing; declining the consent dialog (`ExportConsent::Cancel`) writes **zero** files; a wiped-buffer assertion after each write.

### Acceptance criterion
Selecting N images and confirming the export produces exactly N files on disk
whose checksums match the originally-imported bytes; declining the confirmation
writes nothing; thumbnails are never emitted.

**Status:** ✅ 193/193 tests pass under `scripts/test.sh` and `--asan` (11 new:
4 selection-model + 7 export). The export core is SDL-free and TDD-covered
(`src/ui/export.*`, `selection_model.*`); the consent modal (`consent_dialog.*`),
folder picker (`platform/folder_dialog.*`), gallery multi-select (`Space`/`X`),
and viewer single-image export (`X`) are the SDL plumbing on top.

> **Design note.** Export deliberately violates invariant #1. The mitigation is
> *consent + scope*: a per-export warning, default-cancel, and selection-only
> output. The decrypted bytes still live only in mlock'd memory right up to the
> `write()` call, and the buffer is wiped immediately after.

> **Notes / decisions made during implementation**
> - **Consent baked into the writer.** `export_images(..., ExportConsent)` is a
>   no-op returning `{0,0}` unless `consent == Confirm`, so "declining writes
>   nothing" is enforced in the one place that can write — and is unit-testable
>   headlessly without driving the SDL modal.
> - **Wipe is unconditional.** `export_one_image` `crypto_wipe`s the decrypted
>   `SecureBytes` after the write *whether or not the write succeeded*; a test
>   asserts the buffer is all-zero post-write.
> - **Collision suffixing** appends ` (n)` before the extension
>   (`a.png` → `a (1).png`), never overwriting an existing file; the resolver is
>   pure (`unique_export_path`, filesystem probe injected).
> - **Selection is session/listing-scoped:** `GalleryGrid::refresh()` clears it,
>   so indices are only ever interpreted against the current leaf listing. Click
>   still opens (navigates); `Space` toggles selection on image tiles.
