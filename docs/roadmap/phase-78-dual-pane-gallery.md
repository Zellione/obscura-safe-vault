# Side-by-side dual gallery (Phase 78)

**Status:** ✅ shipped
**Date:** 2026-08-11

## Problem

When managing large galleries across multiple projects or organizing media,
users need to compare, move, or copy items between two galleries quickly.
Previously, switching galleries required navigating away from the current view
and unlocking a secondary vault (Phase 66). A split-pane view would allow
side-by-side browsing of two galleries in the same vault, reducing navigation
overhead for common workflows like "copy a few items from Archive to Project"
without leaving the main view.

## What shipped

### F3 split-pane toggle

- **`F3`** toggles a 50/50 split-view mode (two full `GalleryGrid` panes
  rendering side by side at half-width each) on the same vault. MIN_SPLIT_WIDTH
  900px enforced: split mode is unavailable if the window is narrower.
- **Pane navigation:** `Tab` switches active pane (visual highlight on the
  focused pane). Mouse-click on a tile in either pane activates that pane and
  begins selection.
- **Independent gallery browsing:** each pane maintains independent scroll
  position, selection, and gallery history (ascend/descend navigate within that
  pane only). Both panes render from a shared `Vault`, so gallery structure and
  content are identical, but browsing state is decoupled.

### Pane-to-pane transfer (M in split mode)

- **`M` (Move/Copy)** is available in split mode, acting on the active pane's
  selection and transferring to the other pane's current gallery. Opens the
  **Move/Copy modal** (same flow as Phase 75's Transfer dialog, just
  source/destination pre-determined):
  - **Direction stage** skipped (both vaults are the same).
  - **Mode stage** asks **Move** or **Copy**.
  - **Conflict stage** (Phase 71) runs if same-named galleries exist in the
    destination — **Combine / Rename `_2` / Cancel**.
  - **Running:** one `FileOpJob::start_transfer_collection` with source = active
    pane's gallery, destination = inactive pane's gallery. Completion restores
    both panes' focus and preserves split view.
- **Tag + favorite persistence:** transfers carry source-side effective tags and
  favorite state (Phase 75 materialization semantics unchanged).

### Session-only split configuration restore

- **`DualSessionState`** holds split mode state: `split_active` (true if split
  was on before a viewer round-trip), `has_config` (true if this session ever
  set a left/right gallery pair).
- **On app startup:** split view is off; opening split mode for the first time
  sets both panes to the browsed gallery.
- **Exact restore after viewer round-trip:** exiting the viewer (_Esc_) restores
  the split state from before entry (if `split_active` was true, split remains
  on; panes return to their previous galleries; scroll/selection restored).
- **No persistence across app restart:** split configuration lives only in
  memory (session-only per CLAUDE.md Security).

### Exclusive ops gated in split mode

The following operations are **disabled** in split view (refused with an
informational tooltip or footer message):

- **Shift+M** (dedicated combine dialog — meaningless with two independent panes)
- **Ctrl+D** (duplicate scanner — would scan the whole vault; unclear which
  pane's context applies)
- **Shift+C** (compact — vault-level operation)
- **Quick-switch** (second-vault switcher — both panes share the vault)

Attempting these in split mode shows a brief dismissible hint ("Not available in
split view").

### Key gating in embedded mode

The dual-layout renderer operates with embedded-mode key constraints:

- `GalleryGrid::set_embed_key_allowed(mode)` gates which keys process:
  in embedded mode, only navigation (arrows, Home/End), selection (Space/Ctrl+A,
  B/X for batch ops on the active pane), F1 (help), and Tab (pane-switch) are
  honored. Transfer (M) is routed through the modal, not direct keybinding.
  Delete (Del) works on the active pane only (Phase 74). Other keys
  (`Ctrl+F`/search, `Ctrl+L`/quick-lock, `/` filter) propagate to the parent
  App so they remain global.

## Files added or modified

### New files

- `src/ui/dual_layout.h` / `src/ui/dual_layout.cpp` — 50/50 split layout,
  viewport-rect embedding via `SDL_SetRenderViewport`, MIN_SPLIT_WIDTH 900px
  check, pane sizing math, horizontal divider render.
- `src/ui/dual_session_state.h` — `DualSessionState` struct
  (`split_active`, `has_config`, left/right gallery paths); snapshot/restore.
- `src/ui/dual_gallery.h` / `src/ui/dual_gallery.cpp` — `DualGalleryScreen`
  manager, pane tab/selection/history per pane, F3 toggle logic, viewer
  round-trip coordination.
- `src/ui/dual_transfer.h` / `src/ui/dual_transfer.cpp` — pre-determined
  Move/Copy flow, Conflict stage, `start_transfer_collection` launch helper.
- `tests/ui/test_dual_layout.cpp` — split geometry, MIN_SPLIT_WIDTH enforce.
- `tests/ui/test_dual_transfer.cpp` — move/copy between panes, Conflict
  Combine/Rename, tag/favorite carry-through.

### Modified files

- `src/ui/app.h` / `src/ui/app.cpp` — F3 keybinding, split mode state held in
  `DualSessionState`, pane-aware event routing.
- `src/ui/gallery_grid.h` / `src/ui/gallery_grid.cpp` — new `set_embed_key_allowed`
  method, embedded-mode key filtering, Tab pane-switch propagation.
- `src/ui/image_viewer.h` / `src/ui/image_viewer.cpp` — snapshot/restore split
  state on entry/exit (Esc).
- `src/ui/help_layout.cpp` — F3 help line added ("F3: split gallery" or "Split
  view not available (min 900px width)").
- `src/ui/nav_model.h` — new `NavKind::ToDualGallery` route for returning to
  split view after a viewer visit.

## Test coverage

- `test_dual_layout` — 50/50 split geometry, MIN_SPLIT_WIDTH 900px gate, viewport
  rect sizing.
- `test_dual_gallery` — pane tab switching, independent scroll/selection, history
  per pane, F3 toggle on/off.
- `test_dual_transfer` — move/copy between panes, Conflict Combine/Rename stages,
  tag and favorite persistence through transfer.
- Integration: gallery grid tests verify embed-mode key gating, viewer tests
  verify split state restore after round-trip.

**2029 tests / 0 failed; ASAN clean.**

## Deliberately unchanged

- **`.osv` format:** no new chunks, no format version bump. Dual view is
  session-only memory.
- **`INDEX_VERSION`:** stays 12. Tags and favorites (Phase 75) travel via the
  existing `vault::NodeExtras` on transfer, no schema change.
- **Conflict resolution:** Phase 71 Combine / Rename logic unchanged; pane
  transfer uses the same predicate + Conflict stage as `M` dialog.
- **Tag materialization:** Phase 75 "what you saw is what travels" semantics
  unchanged; effective tags on the sending pane become explicit own tags on the
  receiving side's item.
- **Exclusive ops:** Shift+M, Ctrl+D, Shift+C, quick-switch remain fully
  functional outside split view; split mode simply disables them temporarily as
  a UX clarification, not a code restriction.
