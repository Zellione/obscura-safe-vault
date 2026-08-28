# Module: ui/ — screens, viewer, dialogs, pure models

Index into the UI module's sub-memories. Covers `src/ui/`: every Screen, image/video viewer, all modal dialogs, and pure SDL-free view/search/sort/session models.

## Sub-memory routing

| Topic | Memory |
|-------|--------|
| Text fields, input handling, clipboard | `mem:module/ui/text-input` |
| Screen implementations (gallery, favorites, search, tags, vault manager) | `mem:module/ui/screens` |
| Image/video viewer, playback, slideshow | `mem:module/ui/viewer` |
| Modal dialogs, file pickers, help, settings | `mem:module/ui/dialogs` |
| Pure layout, view models, search, sort, settings state | `mem:module/ui/models` |
| Background jobs: export, delete, transfer, duplicate scan, migration | `mem:module/ui/jobs` |
| Import infrastructure: archives, folders, volume assembly, import queue | `mem:module/ui/import` |

## Key invariants and architecture notes

**Security:**
- Invariant #1: No plaintext to disk (exception: Export, gated by consent modal, Phase 10).
- Password/keyfile held in `crypto::SecureBytes` (mlock'd), wiped on vault lock/app exit.
- Selection text from secure fields always returns empty; clipboard copy/cut refused.

**Models and Views:**
- Text input: `TextInputBase` (all UTF-8/caret/selection logic) → `TextInputModel` (std::string) or `SecureTextInput` (mlock'd buffer).
- Selection: `SelectionModel` (Phase 68) shared by grid, favorites, search results for Space/Ctrl+A.
- Gallery nav: `GalleryGrid` with one-shot scroll-follow (Ensure/Center), persisted sort key, live width reflow.
- Detail panel: right-edge `DetailPanelState` (open/scroll), Phase 48 aggregate view for 2+ selected items, Phase 49 tag chips.

**Background Work:**
- ImportQueue (Phase 50): one worker jthread + decode pool. Per-file pipeline: read → decode → encrypt → append → stage node. Phase 65 batching fix: snapshot+flush fires once per busy→idle, not every idle frame.
- FileOpJob: export / delete / move-copy on bg worker. Phase 76 TransferMode threaded through combine.
- DupScanJob: main-thread snapshot → bg exact+perceptual hash over size-colliding files only, mlock'd SecureBytes wiped immediately.
- MigrationJob (Phase 65): exclusive vault ownership, coordinator + worker pool, 256 MiB mlock budget.

**Phase 75 highlights:**
- TransferDialog: pull direction (From/To), conflict stage (pre-scan for colliding galleries), multi-select on PickSrcGalleries.
- CombineDialog: Move vs Copy mode (Move deletes emptied source).
- Gallery tile sizes: S=224 / M=288 / L=384 / XL=480 / XXL=512 px (Phase 93; was S=192/M=256/L=352/XL=448 after Phase 75, 188 before that). Axis legend:
  the L-key density cycle is shared machine-wide ({gallery grid, favorites/tags, advanced
  search}) via `ui::gallery_view_setting` + `gallery_view.conf`.
- Thumbnail strip: manual scroll state (wheel anywhere in band), strip-visible-range windowing, ±8-cell prefetch margin.

**No logging of key material; no temporary files for decrypted content; random nonces per XChaCha20-Poly1305 encrypt.**
