## Phase 44 — Gallery organization tools 🔜

**Goal:** Four related gaps in gallery/image organization, grouped into one
phase (same multi-part shape as Phase 40/43):

1. The Move/Copy dialog's destination-gallery picker (`TransferDialog`'s
   `PickGallery` stage) draws every eligible gallery in one unbroken list with
   no scrolling or filtering — it overflows the modal once a vault has more
   than a handful of galleries.
2. Nothing in the vault or UI can rename an image, video, or gallery.
3. Mass-move (Space to multi-select, `M` to move) only works on images today;
   selecting a gallery tile is a no-op.
4. There's no way to merge the contents of one gallery into another —
   moving a gallery only relocates it as a whole subtree, and fails outright
   if the destination already has a same-named child.

Full design rationale (brainstorming transcript): see the session that
produced this doc — kept local/untracked per this repo's `docs/`-is-gitignored
convention; this file is the durable summary.

### Part 1 — Scrollable + filterable destination picker

- **`ui/gallery_picker.h/.cpp`** (new) — pure, SDL-free `GalleryPickerModel`:
  holds an item list, a filter string, the filtered/matched subset, a
  selection index, and scroll windowing (`geom(visible_rows)`, ported from
  `tag_overview.cpp`'s `compute_geom`). Filtering reuses the existing
  `ui::tokenize`/`ui::matches` (`search_model.h`) treating each gallery path
  as a bare name with no tags — no new matching logic.
- **`TransferDialog`** (`src/ui/transfer_dialog.*`) — `PickGallery` stage
  switches from the flat `row_list` draw to a `GalleryPickerModel`-backed
  windowed list. `/` (via the existing layout-robust `is_search_key`) opens
  filter typing; typing narrows live; Backspace edits; Esc clears the filter
  before falling through to the dialog's existing close(). The pinned
  `"+ New gallery…"` row stays exempt from filtering.

### Part 2 — Rename images, videos, and galleries

- **`vault::rename_node(Vault& v, string_view gallery_path, string_view old_name, string_view new_name) -> VaultResult`**
  — new free-friend function in `vault.h`/`vault.cpp` (same pattern as
  `gallery_sort_key`/`set_gallery_sort`, keeping `Vault`'s member count under
  the existing cpp:S1448 cap). Validates `is_safe_node_name(new_name)`,
  rejects a same-named sibling (`AlreadyExists`), mutates the node's `name`
  field in place, commits. Since `IndexNode` stores only a local `name`
  (paths are computed on the fly, never persisted), this is a pure leaf-field
  edit — descendants, tags, favorites, sort key, and cover are all untouched.
  Known, accepted limitation: the session-only `GallerySessionState` position
  memory is keyed by path string and isn't migrated on rename (harmless —
  it already tolerates any other structural change the same way).
- **`ui/rename_dialog.h/.cpp`** (new) — small standalone modal class (same
  composition pattern as `ConsentDialog`/`QuickSwitch`: a field on
  `GalleryGrid`, not a growth of the already-bundled `Naming` struct).
  Seeded with the focused tile's current name. Bound to **`R`**.

### Part 3 — Mass move extended to galleries

- `GalleryGrid::toggle_or_open()` extends Space-select to gallery tiles (today
  only `is_image()`).
- `GalleryGrid::start_transfer()`: an all-images selection is unchanged
  (`transfer_images`); an all-galleries selection takes a new path; a *mixed*
  selection is rejected with an error — images and galleries have genuinely
  different eligible-destination sets (`image_target_galleries` = leaf-only
  vs. `gallery_target_parents` = subgallery-holding-only), so there's no
  single destination list a mixed batch could present.
- **`vault::transfer_galleries(src, vector<string> src_paths, dst, dst_parent, mode, progress) -> TransferTally`**
  (new, `vault/transfer.*`) — loops `transfer_gallery` per path, mirroring
  the existing `transfer_images` loop over `transfer_image`.
- `TransferDialog::Source` gains a third variant (`Galleries`, plural)
  alongside today's `Images`/`Gallery`; `FileOpJob::start_transfer_galleries(...)`
  mirrors the existing `start_transfer_images`.

### Part 4 — Combine two galleries

- **`vault/combine.h/.cpp`** (new):
  ```cpp
  struct CombineTally { int media_moved=0, media_skipped=0, galleries_merged=0, galleries_moved=0; };
  [[nodiscard]] VaultResult combine_galleries(Vault& src, std::string_view src_gallery,
                                               Vault& dst, std::string_view dst_gallery,
                                               CombineTally& tally, OpProgress* progress = nullptr);
  [[nodiscard]] std::vector<std::string> combine_target_galleries(const Vault& dst, const Vault& src,
                                                                    std::string_view src_gallery);
  ```
  - Type compatibility: a media-holding gallery only combines with one
    holding no sub-galleries (empty or media-holding); a subgallery-holding
    gallery only combines with one holding no media. `combine_target_galleries`
    filters candidates to compatible ones; a mismatch reaching
    `combine_galleries` anyway is `InvalidArg`.
  - Same-vault cycle check: `dst_gallery` cannot be `src_gallery` or a
    descendant of it (same shape as the existing `is_same_vault_cycle`).
  - Leaf case: every media filename moves `src` → `dst` via `transfer_image`;
    a same-named file already in `dst` is skipped and tallied
    (`media_skipped`), not a hard failure.
  - Folder case: for each sub-gallery child of `src` — no same-named child in
    `dst` → move wholesale via the existing `transfer_gallery`; a same-named
    child exists → recurse `combine_galleries` on that pair.
  - Tags: union, via `dst.add_tag(dst_gallery, tag)` per `src` tag —
    `add_tag` already case-insensitively dedups.
  - Source removal: `src_gallery` is deleted only if it ends up empty
    (checked via `v.list(src_gallery).empty()`, not by counting — robust to
    partial skips). A partially-merged source is left with its remaining
    children; the tally surfaces what didn't move so the user can resolve
    conflicts — via Part 2's rename — and retry.
  - `OpProgress`/cancel semantics match `transfer_gallery`: a cancel leaves
    everything moved so far in place and does not delete the source.
- **`ui/combine_dialog.h/.cpp`** (new). Needs the same "pick a vault, unlock
  it" flow as `TransferDialog`; that fragment (candidate list, `Dest` unlock
  state, `choose_vault`/`try_unlock`, their key handlers and rendering) is
  extracted out of `TransferDialog` into a shared `VaultUnlockPicker`
  component used by both dialogs — the one non-additive refactor in this
  phase, justified because the duplication becomes real (not speculative)
  the moment `CombineDialog` exists. Its own final stage is a
  `GalleryPickerModel`-backed full-tree target picker (`combine_target_galleries`),
  then a `Running` progress stage via new `FileOpJob::start_combine(...)`.
  Triggered by **`Shift+M`**, acting on the *currently browsed* gallery
  (`nav_.path()`), not a focused tile — disabled at root. On completion: same
  vault → navigate into the destination gallery; different vault → navigate
  up to the source's former parent; partial merge → stay put, show the tally
  as a status message.

### Keybindings summary
`R` rename (new) · `Shift+M` combine (new) · `/` also opens the filter box
inside the move dialog's gallery picker (Part 1) · `M`/Space extended to
galleries (Part 3, unchanged for images). `GalleryGrid::help_groups()`
updated for all of the above.

### Testing
Headless, SDL-free vault tests: `rename_node` (image/gallery rename,
collision, invalid name, descendants/tags/favorites unaffected),
`transfer_galleries` (tally, partial cancel), `combine_galleries` (leaf merge
with collisions, recursive folder merge, type-mismatch rejection, cycle
rejection, tag union, partial-merge-leaves-source-intact). Pure UI-model
tests for `GalleryPickerModel` (filter narrows/clears, windowing math,
selection clamping) and `RenameDialog`.

### Acceptance criterion
`scripts/test.sh` and `scripts/test.sh --asan` both green. Manual check: in
a vault with 20+ galleries, open the Move dialog, confirm `/` filters and the
list scrolls; rename an image and a gallery; multi-select and move two
galleries at once; combine two same-named-child folder trees and confirm the
recursive merge + tag union.

### Out of scope (deferred)
- Renaming from the tag overview / favorites / search-result screens (only
  the main gallery grid gets `R` in this phase).
- Undo for combine/mass-move.
- A generalized heterogeneous (images + galleries in one batch) move.
