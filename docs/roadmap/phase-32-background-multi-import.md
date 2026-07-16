## Phase 32 — Background multi-file import ✅

**Goal:** The multi-select file-picker import (already working at the
dialog level since before this phase) no longer blocks the UI thread: it
runs on a background `FileOpJob`, with a progress bar, cooperative
Esc-cancel, and an aggregated success/failure summary — matching every
other bulk vault operation in this codebase.

### Tasks
- [x] `src/ui/file_op_job.{h,cpp}` — `FileOpKind::Import` +
  `FileOpJob::start_import(vault, base_gallery, files)`: reads and imports
  each picked file on the worker thread (dispatching to `add_video`/
  `add_image` by extension), tallying successes/failures. Per-file failures
  never turn into a hard `oc.error` (mirrors `start_export`'s convention);
  the finished-import status names up to 3 failed files plus a "+N more"
  suffix beyond that (the footer is one unwrapped text line).
- [x] `src/ui/gallery_grid.{h,cpp}` — `do_import`'s synchronous per-file loop
  is gone; `pump_import()` launches `naming_.file_op.start_import(...)`
  instead. Every other piece of the job machinery (progress veil, Esc→
  cancel, vault-busy gating, the shared `draw_file_op_progress` modal) was
  already generic over `FileOpKind` and needed only one new wording case.

**Out of scope (YAGNI):** the file-picker dialog itself (multi-select
already worked — `allow_many = true` predates this phase); drag-and-drop
import; recursive folder import; any change to the already-backgrounded
ZIP/CBZ import path.

### Acceptance criterion
Picking multiple files via the existing import dialog no longer freezes the
UI: a progress bar tracks "N / M files", Esc cancels cooperatively leaving
already-imported files intact, and the finished-import status line reports
how many imported and — capped at 3 names plus a "+N more" suffix — which
ones failed.

**Status:** ✅ All tests pass (`scripts/test.sh`); `scripts/test.sh --asan`
clean.
