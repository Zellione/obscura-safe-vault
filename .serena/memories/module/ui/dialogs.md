# Module: ui/dialogs — modal dialogs, pickers, editing

Modal dialogs: transfer/combine operations, file pickers, rename/tag editing, search, help, settings.

## Dialogs
- `transfer_dialog.*` — `M` modal: move OR copy the selection / a focused gallery subtree to
  another vault or within the active vault. Source enum `{Images,Gallery,Galleries,Collection}`
  (Collection: Phase 68 — per-parent `ParentGroup`s + gallery subtrees in one run);
  open()/open_gallery()/open_galleries()/open_collection(); `open_mixed(src, media, galleries)`
  wraps a grid mixed selection as a single-group Collection. Stages: Mode(Move/Copy) →
  PickingDest (delegated to VaultUnlockPicker) → PickGallery (GalleryPickerModel, scrollable +
  `/`-filterable, "+ New gallery…" pinned via set_pinned_suffix) → run `vault::transfer_*`
  per mode (Collection → `FileOpJob::start_transfer_collection`). rebuild_targets: Images uses
  `image_target_galleries`, everything else `gallery_target_parents`. **Phase 66:**
  on completion with a Keep\* mode, the destination handle is handed off to the warm slot via
  `release_to_slot()` before the dialog closes; the slot's sliding reset is called after
  transfer success. Grid skips its import dlg poll while active(); M with no selection acts on
  the focused tile.
- **Phase 71 — TransferDialog `Conflict` stage:** between PickGallery and Running. `do_move` pre-scans on the MAIN thread (race-free — job not launched yet) via `vault::colliding_galleries` over `galleries_for_conflict_scan()` ({src_gallery_} for Gallery, src_galleries_ for Galleries/Collection, {} for Images — media-only never prompts; the typed new-gallery-name path flows through the same pre-scan). ≥1 clash → Conflict stage: "N galleries already exist at destination", rows Combine / Rename with `_2` suffix / Cancel (Up/Down+Enter like the Mode stage; Esc closes). Choice → `launch_transfer(target, CollisionPolicy)` (the old do_move body). Conflict state lives in `struct Conflict {target, count, sel} conflict_` (S1820 fold; reset in open()). Row pitch via `ui::line_pitch`. `FileOpOutcome` gained `skipped` (status line appends "(N skipped)"; skips never reach the failure list); `file_op_job.cpp` bundles counts in `TransferCounts` and gallery-run args in `GalleryTransferSpec` (S107/S1188); `CollectionTransferSpec{groups, gallery_paths}` is now PUBLIC in file_op_job.h and `start_transfer_gallery/galleries/collection` take a `vault::CollisionPolicy` before `label` (`start_transfer_media_grouped` forwards Fail).
- **Phase 75 — TransferDialog pull direction:** Stage enum gained `Direction` (first stage when
  the host called `set_current_gallery` — the grid does before all five `open*` sites; collection
  screens don't, keeping push-only) and `PickSrcGalleries`. Pull state bundled in `struct Pull
  {active, current_gallery, has_current, direction_sel} pull_` (S1820 cap); render helpers
  `render_direction_body`/`render_mode_body` are file-local (S1448 cap). Pull flow: Direction=From
  → Mode → PickingDest with `picker_dest_.open(src_path_, /*include_self=*/false)` + "Source
  vault:" title → PickSrcGalleries (`picker_.set_items(vault::all_galleries(dest_vault())`,
  `set_multi(true)`, no pinned suffix; Space toggles, Enter → `ui::drop_descendant_paths(checked)`
  → `colliding_galleries(src_, current_gallery, paths)` pre-scan → Conflict or launch). The target
  is threaded EXPLICITLY through `launch_current(target, policy)` in all four paths (push/pull ×
  conflict/no-conflict — a regression once launched conflict-free pushes with an empty
  `conflict_.target`). Pull launch: `start_transfer_galleries(dest_vault()/*source*/, paths,
  src_/*active=destination*/, current_gallery, mode_, policy, label)`; completion status rewrites
  " to " → " from "; warm-slot `release_to_slot()` is direction-agnostic. Header reads "Pull from
  another vault" throughout the pull stages.
- `combine_dialog.*` — `Shift+M` modal: merges the CURRENTLY BROWSED gallery into another via
  `vault::combine_galleries` (same- or cross-vault; mixed galleries combine fine — media
  children first, then sub-gallery children). Stages Mode (Move/Copy, Phase 76 — Move deletes
  the emptied source, Copy leaves the source fully untouched) -> PickingDest
  (VaultUnlockPicker) -> PickTarget (GalleryPickerModel over `combine_target_galleries`) ->
  Running (progress modal). The mode threads through `FileOpJob::start_combine(…, TransferMode,
  label)` into `combine_galleries`; outcome verbs follow it ("N copied" vs "N moved").
  `CombineOutcome{status,source_gone,same_vault,dest_path}` drained by GalleryGrid::update()
  for post-combine nav: source_gone && same_vault -> jump_to_gallery(dest_path); source_gone &&
  !same_vault -> go_up(); !source_gone -> refresh() (partial merge from a collision).
  source_gone read as `mode==Move && src_.list(src_gallery_).empty()` (Copy never removes the
  source, so an empty source must not report gone). **Phase 66:** on completion with a
  Keep\* mode, the destination handle is handed off via `release_to_slot()` and the slot's
  sliding reset called after success.
- `failure_list_dialog.*` — **Phase 67 modal**, shown when a `TransferDialog` or `CombineDialog` 
  completes with `failures` in the `TransferCompletion` / `CombineOutcome`. Lists each failed 
  item (node name only) + its ASCII reason (`SizeMismatch`, `NotAnImage`, `ProbeFailure`, etc.). 
  Scrollable if many failures; Esc or a close button dismisses. Owned by `GalleryGrid`; opened 
  by `gallery_grid::update()` when it drains a completed transfer/combine with failures. Takes 
  the `TransferCompletion` hand-off from `TransferDialog` (via the result queue, not live state) 
  to populate the list. Render draws the modal veil + a centered box listing "Failed items:" 
  header, then rows of `name: reason` in a fixed-pitch font for clarity.
- `vault_unlock_picker.*` — "pick a destination vault, then unlock it" flow, extracted so both
  TransferDialog + CombineDialog reuse it. Stages PickVault ("This vault" row 0, or a registry
  entry) -> Unlock (password + optional keyfile, skipped for "This vault"). Esc cancels the
  whole flow. Owns a transient dest `vault::Vault`; `close()` is idempotent (locks/wipes only
  if actually unlocked). `is_self()`/`unlocked_vault()` combine with the caller's active vault
  to resolve "the vault to write into". **Phase 66:** the Unlock stage gains an Up/Down mode
  selector (LockNow/KeepTimed/KeepSession); skips Unlock entirely when the picked destination
  matches the warm slot (password-free); and calls `release_to_slot(mode)` on completion with
  a Keep\* mode so the destination stays warm instead of being wiped. **Phase 75:**
  `open(src_path, bool include_self = true)` — false omits the "This vault" row (pull must not
  offer the active vault as its own source; selection→registry index mapping shifts by the
  include_self offset), and `render` takes a title override so the pull flow labels the stage
  "Source vault:". The picked vault serves as transfer SOURCE in pull — the slot and
  release_to_slot are direction-agnostic ("the other vault").
- `gallery_picker.*` — `GalleryPickerModel`: pure SDL-free filterable/scrollable list model
  shared by TransferDialog + CombineDialog. set_items, open/close_filter (`/`), filter_*,
  move(delta), filtered(), selected(), geom(visible_rows). `set_pinned_suffix(item)` keeps one
  extra row appended after filtering, exempt from the filter. Phase 54: the filter is a
  `TextInputModel` exposed as `filter_input()`; hosts route SDL events into it and call
  `refilter()` when its `revision()` moved. ONE model, TWO drivers
  (`transfer_dialog.cpp` + `combine_dialog.cpp`) — both routing blocks must stay in
  step, and both entry points (`M` and `Shift+M`) need re-testing on any change.
  **Phase 75 — opt-in multi-select:** `set_multi(bool)` (CLEARED by `set_items`), `multi()`,
  `toggle_checked()` (current filtered row; pinned-suffix row toggles nothing; no-op unless
  multi), `is_checked(item)`, `checked()` (items_ order). Both legacy drivers stay single-select —
  only the pull PickSrcGalleries stage opts in. Free fn `ui::drop_descendant_paths(paths)`
  (gallery_picker.h): strict-descendant prune with "/" separator ("ab" is not under "a"),
  sorted-order output. Tests: test_gallery_picker.cpp.
- `folder_dialog.*` (Phase 51) — `FolderDialog`: native file-picker for folders. `Purpose` enum:
  `{None, Export, ImportFolder}`. `open(purpose, allow_many)` → SDL_ShowOpenFileDialog with
  SDL_DIALOG_FOLDER. `take_result(purpose)` returns `vector<std::string>` — phase-scoped
  filtering so export and folder-import results don't cross. Single-select result is
  `result[0]`, multi-select is the whole vector. Mirrored from FileDialog architecture.
  Backed by `FolderDialogTestPeer` for headless tests. Both export call sites guard
  `!result.empty()` before indexing.
- `rename_dialog.*` — `R` modal, renames the focused image/video/gallery via
  `vault::rename_node`. open(gallery_path,old_name)/close()/handle_event/render/
  consume_completed(status_out).
- `tag_editor.*` — `G` add/remove-tags modal in GalleryGrid + ImageViewer. Current-tags list
  scrolls (Up/Down) via pure `tag_scroll.h` + auto-scrolls to a just-added tag. Read-only
  "Inherited from gallery" section (`ui::inherited_tags`, `tag_inherit.*`) below own-tags;
  Del/selection never touch it. **Phase 51:** Read-only "From contents" section below inherited
  (galleries only, non-empty only) — computed by `ui::contents_tags`, threaded by the tag
  editor's `from_contents_` vector. Del/selection never touch it, move_cursor bounds selected_
  to tally_ only (structurally impossible to delete a tag not in the node's own tags).
  Autosuggest dropdown while typing (`VaultSearch::all_tags` vocab refreshed on open/add/remove;
  `ui::editor_tag_suggestions`; Up/Down highlight via move_tag_cursor, -1=buffer; Enter adds the
  TYPED text unless a suggestion is highlighted; Esc deselects first). **Phase 56:** list layout
  derives from `list_layout.*` module.
- `search_overlay.*` — `/` live-filtered search modal in GalleryGrid; Tab cycles scope
  (Both/Images/Galleries). **Phase 56:** list layout derives from `list_layout.*` module.
- `consent_dialog.*` — modal "export anyway?" confirm (SDL plumbing).
- `theme_picker.*` — **DELETED in Phase 49.** Its behaviour moved into the `F2` settings
  overlay's Appearance section (`settings_overlay.*`); the vault manager's `C` now emits
  `NavKind::ToSettings`, which App translates into `open_settings(..., Appearance)`.
- `quick_switch.*` — global `` ` `` (grave) overlay: lists registry vaults; choosing one emits
  `NavKind::ToUnlock(path)` (App locks current + unlocks chosen); Esc or the active vault =
  no-op. Hosted by GalleryGrid, ImageViewer, FavoritesScreen base (take a VaultRegistry& +
  active vault path). `consume_choice()` drains the pick. **Phase 66:** if the warm vault matches
  the picked vault, selecting it promotes the warm handle to active without a password prompt
  (the slot then empties).
- `second_vault.*` (Phase 66) — `ui::SecondVaultSession`: the app-owned warm slot holding a
  destination vault unlocked across multiple transfers. Pure model in the header: sliding reset
  on completed transfer, tick/expiry, defer-while-job-running, replace-on-new-destination,
  explicit wipe. `SecondVaultStatus` / `second_vault_status()` / `format_keep_open_left(secs)`
  are the global-snapshot readers for badge rendering. Released/wipes in transitions to
  LockActive, vault switch, LockSecond, or app exit. Tested alongside App in integration tests.
- `progress_modal.*` — `draw_op_progress`: shared veil + "N/M" bar + cancel-hint modal reused
  by every screen hosting a background job.
- `help_popup.*` — shared `F1` help popup: HelpGroup/HelpEntry types + pure open/close/scroll
  logic + `draw_help_popup`. `Screen::help_groups()` virtual (default empty) supplies
  per-screen grouped content; App owns HelpPopupState + intercepts F1 globally. Esc/Q close.
  **Phase 51:** `HelpPopupState::scroll_line` is an int line index (not pixels); `clamp_help_scroll`
  retired, replaced by `clamp_help_line`. Clip band sized to `visible_lines * LINE_H`; clip rect
  via `lround`, not truncation. `help_layout.*` new module: `help_visible_lines(popup_h, lines_per_column)`
  returns the number of lines that fit in a viewport; `HelpColumn` / `help_column_count(groups, lines_per_column)` /
  `pack_help_columns(groups, lines_per_column)` handle two-column packing above a width threshold,
  never splitting a group across a column boundary. Single-column fallback below the threshold.
  Scroll affordance from theme TEXT_FAINT when content overflows. Tests verify first/last line fully
  inside the band, every line reachable, group boundaries never split, column budgets never exceeded.
- `settings_model.*` (Phase 49, pure, SDL-free) — state behind the `F2` overlay. **Phase 54:
  `SettingsState::prompt_buf` is a `TextInputModel`, so `SettingsState` is now non-copyable
  and non-movable** (every use was already by reference; only the test fixture changed, and
  `state = {}`-style resets are not available). Contents:
  `SettingsSection{Appearance,Browsing,TagColours,Security}` + `SETTINGS_SECTION_COUNT = 5`, and
  `SettingsState{section,in_pane,row,open,vault_unlocked,draft,theme,prompting,prompt_row,
  prompt_buf,error}`. `settings_move_section` (clamps, resets row), `settings_move_row`
  (clamps), `settings_change_value` (theme wraps both ways; default sort steps via
  next_/prev_sort_key SKIPPING `Default`, which is meaningless as a vault default; tile flag
  toggles; a category row wraps its swatch mod 16; Security section cycles mode via
  next_/prev_second_vault_mode), `settings_row_count` (Appearance always 1; Security always 1
  machine-scoped row; the two vault sections 0 unless `vault_unlocked`), and category CRUD
  `settings_add_category`/`settings_rename_category`/`settings_remove_category` (trim, reject
  blank/duplicate via `ui::tag_ci_equal`, honour `INDEX_MAX_CATEGORY_BYTES`/
  `INDEX_MAX_TAG_CATEGORIES`). NOTE: the CRUD/value entry points do NOT themselves check
  `vault_unlocked` — they are memory-safe either way, so the OVERLAY must not route value keys
  into a locked vault's sections. **Phase 66:** Security section added for cross-vault keep-open
  mode preferences (LockNow/KeepTimed/KeepSession), machine-scoped via `platform::SecondVaultPref`
  (persisted in second_vault.conf beside theme.conf).
- `settings_overlay.*` (Phase 49, SDL) — draws the overlay (veil + section rail + row pane +
  footer) and handles its input: `open_settings`, `close_settings(state, window)`,
  `handle_settings_event(state, window, event, commit_out)` (takes the full SDL_Event, not a
  keycode, because the add/rename prompt needs SDL_EVENT_TEXT_INPUT and
  SDL_StartTextInput/StopTextInput need a Window), and `draw_settings_overlay`. Theme rows
  apply live and persist exactly as the retired ThemePicker did. Not in `osv_tests`. **Phase 66:**
  Security section displays and cycles the default cross-vault keep-open mode via Up/Down.
