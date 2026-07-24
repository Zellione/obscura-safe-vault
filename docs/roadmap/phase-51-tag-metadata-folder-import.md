## Phase 51 — Tag metadata, folder import & organisation polish ✅

**Goal:** Make a vault's structure and tag vocabulary legible. Tags gain a
free-text description surfaced in the tag overview; tags roll up from a gallery's
contents as well as down from its ancestors; a folder can be imported as a gallery
with its subfolders mirrored as sub-galleries; archive and folder picks accept
multiple selections; sub-gallery tiles show what is directly inside them; and the
`F1` help popup stops clipping its first and last lines.

**Final status:** 26 commits, 1229 tests / 0 failed (plain, ASAN, TSan). Baseline at phase start was 1165 tests.

Full design, including the storage rationale and the recorded tradeoffs:
[`docs/superpowers/specs/2026-07-24-tag-metadata-folder-import-design.md`](../superpowers/specs/2026-07-24-tag-metadata-folder-import-design.md).

### Tasks

**1. Tag descriptions**
- [x] Index format: `tag_descriptions` sub-block (`u16 count` + per entry `{u16 name_len, name, u16 desc_len, desc}`) appended to the Phase 49 vault-global settings block. `INDEX_VERSION` **8 → 9**; pre-v9 blobs read with an empty map. Caps `INDEX_MAX_TAG_DESCRIPTIONS = 4096` / `INDEX_MAX_TAG_DESC_BYTES = 512`, out-of-range **rejected, not clamped**. Fuzz corpus seeds descriptions.
- [x] `vault::set_tag_description()` / `vault::find_tag_description()` — keys matched case-insensitively via `ui::tag_ci_equal` (first-seen casing kept, matching `add_tag`); an empty description **removes** the entry. Persisted via the existing crash-safe commit.
- [x] `ui::TagTally` gains `description`, populated by `VaultSearch::tag_overview()`; the pure sort/filter model stays pure. Filter continues to match **names only**.
- [x] `TagOverviewScreen`: two-line rows (chip + counts, then dim description or `(no description — [E] to add)`); `[E]` inline edit prompt reusing the settings-overlay pattern; failed save surfaces on the error line, never as success. Description drawn via `ui::fit_text`. Rows-per-page maths moves into the pure model.
- [x] Tests: v9 round-trip, v8 → empty, over-cap/over-length rejection, fuzz; vault set/get/ci-match/empty-removes/lock-reopen; model carries description through sort+filter; pagination incl. a viewport too short for one row.

**2. Help popup — two columns, line-quantised scroll, full height**
- [x] **Reproduce first.** Headless reproduction confirmed (Task 5): the defect occurs for windows under 600 px tall, where `ph = min(H-80, 520)` yields a fractional line count; windows ≥ 600 px are clean because the viewport is exactly 17 lines (408 px). With pixel-tracked scroll, content can rest unaligned to LINE_H boundaries at viewport edges — at maximum scroll the partial line is at the top, at rest it is at the bottom, and mid-scroll shows both (the owner's observed state). Four characterisation tests built on a faithful geometry replica now fail for H < 600 as expected and are parked pending the fix.
- [x] `HelpPopupState::scroll_line` is an integer **line index**; `clamp_help_line` (retired `clamp_help_scroll`) clamps to `max(0, total_lines - visible_lines)`.
- [x] Clip band sized to `visible_lines * LINE_H`; clip rect via `lround`, not truncation.
- [x] Popup height `min(H - 80, needed_height)` — drop the hard 520 px cap.
- [x] Pure `pack_help_columns(groups, lines_per_column)`: two columns above a width threshold, **never splitting a group** across a boundary; one-column fallback below it; scrolling retained when two columns still overflow.
- [x] Scroll-position affordance so it is visible that content continues.
- [x] Tests: first line fully inside the band at scroll 0; last line fully inside at max scroll; every line index reachable; packing never splits a group and never exceeds a column budget; single-column fallback. Verified against the widest (gallery grid) and narrowest (unlock) help sets.

**3. Folder import as gallery (`[O]`)**
- [x] `platform::FolderDialog` gains a `Purpose` enum + `take_result(Purpose)` overload, mirroring `FileDialog` — **prerequisite**, since `gallery_grid.cpp` already drains the untagged folder result for export destinations.
- [x] `ui::folder_scan.*` — `scan_folder(root)` via `recursive_directory_iterator` with `skip_permission_denied`, **symlinks skipped** (cycles / root escape), emitting archive-style relative paths as `ZipEntry`; bounded by an entry-count limit.
- [x] Feed **`build_zip_plan()` unchanged** — mirrored sub-gallery tree, ancestors before children, per-component `sanitize_node_name`, unsupported files skipped + counted. No new tree-building code.
- [x] `ImportTaskKind::Folder` + `ImportQueue::enqueue_folder()` + `process_folder_task()` mirroring `process_archive_task()`, driving the same `StagingSink`. Resequencer, decode pool, `CommitLane`, progress, cancel, `Shift+I` status and footer summary all unmodified.
- [x] Naming popup prefilled with the folder basename.
- [x] Tests: scan over a temp tree (nesting, unsupported skipped, empty dirs, symlink skipped, permission-denied non-fatal); plan mirrors the tree; queue-level import survives lock/reopen.

**4. Multi-select**
- [x] **Investigate `[I]` per `superpowers:systematic-debugging`.** Every readable layer is already correct — `allow_many = true` (`file_dialog.cpp:58`), `on_files()` loops the whole `filelist`, `pump_import()` converts all of it, `enqueue_files()` stores the whole vector. Instrument the count the callback actually receives, locate where entries are lost, then fix. **No speculative change.** → **CLOSED: NO DEFECT.** Owner verified on 2026-07-24 that `[I]` multi-select DOES work.
- [x] `open_zip()` → `allow_many = true`; one `ImportTaskKind::Archive` task per archive.
- [x] `FolderDialog::open()` → `allow_many = true`; one gallery per folder.
- [x] **Naming for bulk archive picks (recorded assumption):** >1 archive picked → each auto-named from its own `meta.json` title falling back to its filename stem (`meta_gallery_name()`), **no prompt**. A single pick keeps today's prefilled prompt.
- [x] Mixed files+folders in one native dialog is **not** delivered — SDL3 exposes the two separately. `[I]` and `[O]` feed the same queue, so back-to-back picks land as one continuous batch.
- [x] Tests: multi-entry `filelist` yields every path; an unusable path drops without dropping siblings; N archives → N tasks with N distinct names; N folders → N galleries; an import-folder result is not drained by the export handler.

**5. Sub-gallery tile counts (direct children)**
- [x] Pure `direct_child_counts(node)` + `format_tile_counts(counts)` — plural-aware in the style of `describe_subtree`: `"3 galleries · 12 items"` / `"1 gallery · 1 item"` / `"12 items"` / `"empty"`. **"items" is images + videos combined** — `direct_child_counts` returns them separately (reusing `SubtreeCounts`), and the formatter collapses them, because a tile has no room for three numbers and the `[D]` panel already breaks them out.
- [x] Dim line beneath the tile label on gallery tiles; gallery rows in List view gain the same in their metadata columns.
- [x] Geometry follows the Phase 49 chip-row precedent: space reserved **per gallery listing, never per tile** (no sub-galleries in the listing → nothing reserved); the cell does **not** grow — label moves up, thumbnail shrinks by the row height — so every grid metric and hit-testing are untouched.
- [x] Counts cached parallel to `children_`, rebuilt in `refresh()`.
- [x] **Recorded tradeoff (owner's explicit choice):** direct-only deliberately disagrees with the `[D]` panel's recursive `count_subtree` tally on deep trees. Label wording makes "directly inside" unambiguous.
- [x] Tests: empty / media-only / galleries-only / mixed, and that nested content is not counted; singular/plural/zero-drop formatting; per-listing reservation predicate mirrors `any_chips_to_show`.

**6. Tag roll-up from contents**
- [x] Pure `ui::contents_tags(vault, gallery_path)` mirroring `ui::inherited_tags`: descendant walk, ci de-dupe, minus own **and** inherited tags, depth-bounded by `INDEX_MAX_DEPTH`. Nothing stored — read-time only, exactly like the Phase 12 downward cascade.
- [x] Read-only **"From contents"** section in the tag editor (below "Inherited from gallery"; `Del`/selection never touch it) and in the `[D]` detail panel (marked `DetailSection::is_tags`, so it renders as chips with no new drawing code).
- [x] `search_dfs` / `adv_search_dfs`: a **gallery** also matches a descendant's tag. Roll-up computed **bottom-up in one post-order pass** — the per-gallery-call version is O(n²) on deep trees.
- [x] **Deliberately unchanged:** `VaultSearch::tag_overview()` counts stay direct-tag only (Phase 22 chose this so the cascade could not inflate counts; rolling up would inflate them symmetrically).
- [x] Tests: union across a nested tree, excludes own+inherited, ci de-dupe; a gallery matches a deep descendant's tag; leaf gallery empty; `tag_overview` counts unchanged (Phase 22 regression guard); depth bound holds.

**Cross-cutting**
- [x] Update `CLAUDE.md` if conventions change, ROADMAP index row, and the affected Serena memories (`mem:vault_format` for v9, `mem:module/ui` for the new ui modules, `mem:module/vault` for the tag-description API, `mem:ui_spec` for the tag overview + help popup + tile changes).
- [x] `scripts/gen.sh` after adding source files so `compile_commands.json` stays accurate.

> **Merge note:** this branch and `phase-52-legacy-video-codecs` each add one row to
> the ROADMAP index directly after Phase 50, so whichever merges second conflicts
> there. Resolution is trivial — keep both rows in numeric order.

### Notes / decisions made during implementation

**Help-popup root cause (Tasks 5–8).** NOT an off-by-one as originally suspected. The actual cause:
`ph = min(H-80, 520)` pins the viewport height at exactly 17 lines (408 px) for windows ≥600 px tall,
but leaves a **fractional line count** for windows below that threshold. With pixel-tracked scroll, content
rests unaligned to LINE_H boundaries at viewport edges — at maximum scroll the partial line is at the TOP,
at rest it is at the BOTTOM, and mid-scroll shows BOTH (matching the owner's reported screenshot state).
Fixed by converting `HelpPopupState::scroll` from pixels to a **line index**, sizing the clip band to
exactly `visible_lines * LINE_H`, and clipping via `lround` not truncation.

**Task 13 closed as NO DEFECT.** Owner verified on 2026-07-24 that `[I]` multi-select DOES work.
The original report was the premise for this task; investigation is therefore moot and no speculative fix
was made (which is exactly why the task was written as "diagnose, don't guess"). The dialog layer is
already guarded headlessly by `file_dialog_images_result_not_taken_by_zip_purpose`, which asserts a
2-entry Purpose::Images result survives to take_result; the `allow_many=true` argument itself cannot be
asserted headlessly (it is an argument to SDL_ShowOpenFileDialog).

**Three real bugs found and fixed during review (Tasks 12, 14):**
- **Cancelled-folder-import leak on Esc** (Task 12): the Esc branch of `handle_naming_key` cleared `naming_.zip.active`
  but NOT `naming_.folder.active`, so `O → pick → Esc → later N+name` would silently run the cancelled folder import
  under the new gallery's name and never create the gallery. A second instance of the identical leak was found in
  `finish_naming()`'s empty-buffer path. Both verified fixed.
- **Permissive folder-import assertions** (Task 11): 7 assertions used `size() >= N` (Phase 50 anti-pattern that let a
  pipeline wedge hide for a whole task). Replaced with `CHECK_EQ` + child-name/type checks. Tightening revealed NO defect
  — the import tree was correct all along, now actually proven so.
- **Silent discard of queued archives** (Task 14): when a bulk password prompt was cancelled, queued archives were silently
  dropped with no feedback. Fixed by adding a status message reporting how many archives were discarded instead of dropping them silently.

**Recorded design tradeoff (owner's explicit choice, Task 5).** Tile counts are DIRECT children only and deliberately disagree
with the `[D]` panel's recursive `count_subtree` tally on deep trees. Label wording makes "directly inside" unambiguous.

**Recorded assumption on bulk archive naming (Task 12).** >1 archive picked → each auto-named from its own `meta.json` title
falling back to its filename stem via `meta_gallery_name()`, with **no prompt**. A single pick keeps today's prefilled prompt.

**`build_node_details` architecture (Task 19).** Gained `from_contents` parameter with NO default and NO overload, deliberately
— a default is how the panel and the editor would silently drift. This enforces the caller to be explicit.

**Deliberately NOT changed:** `VaultSearch::tag_overview()` counts stay direct-tag only (Phase 22 rule preserved).
**Not delivered:** mixed files+folders in a single native dialog — SDL3 exposes file and folder dialogs separately.
`[I]` and `[O]` feed the same queue instead.

**MINORs noted for final review (Phase 51 ledger):**
- `open_zip()` prompt (Task 4): redundant SDL_StartTextInput on prompt open (on_enter already started it).
- Folder import (Task 11): O(n) vector::erase(begin()) in `queued_archives` — negligible at expected sizes.
- Archive bulk naming (Task 14): the implementation's report overstated complexity as O(n) when it is O(N*T^2) per the
  per-tag ci de-dupe linear scan; requirement (no per-gallery subtree re-walk) is met; T bounded by INDEX_MAX_TAGS and
  small in practice.
- Tile counts (Task 16): counts row borrows CHIP_LINE_H (30px, sized for chip row) for plain text; dedicated 16–20px
  constant would be more honest and give small tiles back ~14px of thumbnail.
- Detail panel "From contents" (Task 19): a "Phase 51" comment in tag_editor.cpp could name the task more precisely.

### Acceptance criterion

A tag description round-trips through lock/reopen and is visible and editable in the
tag overview; a pre-v9 vault opens with no descriptions and no visible change. The
`F1` popup never renders a partially-clipped line at either edge at any window size,
and reflows into two columns when wide. A picked folder imports as a gallery whose
subfolders mirror 1:1 into sub-galleries, in the background queue, browsable while
it runs. Multiple files, multiple archives and multiple folders can each be selected
in one pick and land as separate queue tasks. A sub-gallery tile states what is
directly inside it without changing any grid metric. A gallery shows — and is found
by searching for — the tags carried by its contents, with tag-overview counts
unchanged. All tests pass under `scripts/test.sh` and `--asan`.

**Status:** ⬜ Not started
