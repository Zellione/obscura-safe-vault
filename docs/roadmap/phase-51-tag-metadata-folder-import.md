## Phase 51 — Tag metadata, folder import & organisation polish ⬜

**Goal:** Make a vault's structure and tag vocabulary legible. Tags gain a
free-text description surfaced in the tag overview; tags roll up from a gallery's
contents as well as down from its ancestors; a folder can be imported as a gallery
with its subfolders mirrored as sub-galleries; archive and folder picks accept
multiple selections; sub-gallery tiles show what is directly inside them; and the
`F1` help popup stops clipping its first and last lines.

Full design, including the storage rationale and the recorded tradeoffs:
[`docs/superpowers/specs/2026-07-24-tag-metadata-folder-import-design.md`](../superpowers/specs/2026-07-24-tag-metadata-folder-import-design.md).

### Tasks

**1. Tag descriptions**
- [ ] Index format: `tag_descriptions` sub-block (`u16 count` + per entry `{u16 name_len, name, u16 desc_len, desc}`) appended to the Phase 49 vault-global settings block. `INDEX_VERSION` **8 → 9**; pre-v9 blobs read with an empty map. Caps `INDEX_MAX_TAG_DESCRIPTIONS = 4096` / `INDEX_MAX_TAG_DESC_BYTES = 512`, out-of-range **rejected, not clamped**. Fuzz corpus seeds descriptions.
- [ ] `vault::set_tag_description()` / `vault::tag_description()` — keys matched case-insensitively via `ui::tag_ci_equal` (first-seen casing kept, matching `add_tag`); an empty description **removes** the entry. Persisted via the existing crash-safe commit.
- [ ] `ui::TagTally` gains `description`, populated by `VaultSearch::tag_overview()`; the pure sort/filter model stays pure. Filter continues to match **names only**.
- [ ] `TagOverviewScreen`: two-line rows (chip + counts, then dim description or `(no description — [E] to add)`); `[E]` inline edit prompt reusing the settings-overlay pattern; failed save surfaces on the error line, never as success. Description drawn via `ui::fit_text`. Rows-per-page maths moves into the pure model.
- [ ] Tests: v9 round-trip, v8 → empty, over-cap/over-length rejection, fuzz; vault set/get/ci-match/empty-removes/lock-reopen; model carries description through sort+filter; pagination incl. a viewport too short for one row.

**2. Help popup — two columns, line-quantised scroll, full height**
- [x] **Reproduce first.** Headless reproduction confirmed (Task 5): edge clipping manifests at **all realistic window heights ≥192px**, including H≥600 where ph=520 exactly. The defect is not caused by fractional line counts alone; the root cause is pixel-tracked scroll causing lines to land at non-integer multiples of LINE_H at viewport edges. Three characterisation tests built on a faithful geometry replica now fail as expected and are parked pending fix (Tasks 6–8). Working theory about "ph==520 being the only clean case" is **REFUTED**.
- [ ] `HelpPopupState::scroll` becomes an integer **line index**; `clamp_help_scroll` clamps to `max(0, total_lines - visible_lines)`.
- [ ] Clip band sized to `visible_lines * LINE_H`; clip rect via `lround`, not truncation.
- [ ] Popup height `min(H - 80, needed_height)` — drop the hard 520 px cap.
- [ ] Pure `pack_help_columns(groups, lines_per_column)`: two columns above a width threshold, **never splitting a group** across a boundary; one-column fallback below it; scrolling retained when two columns still overflow.
- [ ] Scroll-position affordance so it is visible that content continues.
- [ ] Tests: first line fully inside the band at scroll 0; last line fully inside at max scroll; every line index reachable; packing never splits a group and never exceeds a column budget; single-column fallback. Verified against the widest (gallery grid) and narrowest (unlock) help sets.

**3. Folder import as gallery (`[O]`)**
- [ ] `platform::FolderDialog` gains a `Purpose` enum + `take_result(Purpose)` overload, mirroring `FileDialog` — **prerequisite**, since `gallery_grid.cpp` already drains the untagged folder result for export destinations.
- [ ] `ui::folder_scan.*` — `scan_folder(root)` via `recursive_directory_iterator` with `skip_permission_denied`, **symlinks skipped** (cycles / root escape), emitting archive-style relative paths as `ZipEntry`; bounded by an entry-count limit.
- [ ] Feed **`build_zip_plan()` unchanged** — mirrored sub-gallery tree, ancestors before children, per-component `sanitize_node_name`, unsupported files skipped + counted. No new tree-building code.
- [ ] `ImportTaskKind::Folder` + `ImportQueue::enqueue_folder()` + `process_folder_task()` mirroring `process_archive_task()`, driving the same `StagingSink`. Resequencer, decode pool, `CommitLane`, progress, cancel, `Shift+I` status and footer summary all unmodified.
- [ ] Naming popup prefilled with the folder basename.
- [ ] Tests: scan over a temp tree (nesting, unsupported skipped, empty dirs, symlink skipped, permission-denied non-fatal); plan mirrors the tree; queue-level import survives lock/reopen.

**4. Multi-select**
- [ ] **Investigate `[I]` per `superpowers:systematic-debugging`.** Every readable layer is already correct — `allow_many = true` (`file_dialog.cpp:58`), `on_files()` loops the whole `filelist`, `pump_import()` converts all of it, `enqueue_files()` stores the whole vector. Instrument the count the callback actually receives, locate where entries are lost, then fix. **No speculative change.**
- [ ] `open_zip()` → `allow_many = true`; one `ImportTaskKind::Archive` task per archive.
- [ ] `FolderDialog::open()` → `allow_many = true`; one gallery per folder.
- [ ] **Naming for bulk archive picks (recorded assumption):** >1 archive picked → each auto-named from its own `meta.json` title falling back to its filename stem (`meta_gallery_name()`), **no prompt**. A single pick keeps today's prefilled prompt.
- [ ] Mixed files+folders in one native dialog is **not** delivered — SDL3 exposes the two separately. `[I]` and `[O]` feed the same queue, so back-to-back picks land as one continuous batch.
- [ ] Tests: multi-entry `filelist` yields every path; an unusable path drops without dropping siblings; N archives → N tasks with N distinct names; N folders → N galleries; an import-folder result is not drained by the export handler.

**5. Sub-gallery tile counts (direct children)**
- [ ] Pure `direct_child_counts(node)` + `format_tile_counts(counts)` — plural-aware in the style of `describe_subtree`: `"3 galleries · 12 items"` / `"1 gallery · 1 item"` / `"12 items"` / `"empty"`. **"items" is images + videos combined** — `direct_child_counts` returns them separately (reusing `SubtreeCounts`), and the formatter collapses them, because a tile has no room for three numbers and the `[D]` panel already breaks them out.
- [ ] Dim line beneath the tile label on gallery tiles; gallery rows in List view gain the same in their metadata columns.
- [ ] Geometry follows the Phase 49 chip-row precedent: space reserved **per gallery listing, never per tile** (no sub-galleries in the listing → nothing reserved); the cell does **not** grow — label moves up, thumbnail shrinks by the row height — so every grid metric and hit-testing are untouched.
- [ ] Counts cached parallel to `children_`, rebuilt in `refresh()`.
- [ ] **Recorded tradeoff (owner's explicit choice):** direct-only deliberately disagrees with the `[D]` panel's recursive `count_subtree` tally on deep trees. Label wording must make "directly inside" unambiguous.
- [ ] Tests: empty / media-only / galleries-only / mixed, and that nested content is not counted; singular/plural/zero-drop formatting; per-listing reservation predicate mirrors `any_chips_to_show`.

**6. Tag roll-up from contents**
- [ ] Pure `ui::contents_tags(vault, gallery_path)` mirroring `ui::inherited_tags`: descendant walk, ci de-dupe, minus own **and** inherited tags, depth-bounded by `INDEX_MAX_DEPTH`. Nothing stored — read-time only, exactly like the Phase 12 downward cascade.
- [ ] Read-only **"From contents"** section in the tag editor (below "Inherited from gallery"; `Del`/selection never touch it) and in the `[D]` detail panel (marked `DetailSection::is_tags`, so it renders as chips with no new drawing code).
- [ ] `search_dfs` / `adv_search_dfs`: a **gallery** also matches a descendant's tag. Roll-up computed **bottom-up in one post-order pass** — the per-gallery-call version is O(n²) on deep trees.
- [ ] **Deliberately unchanged:** `VaultSearch::tag_overview()` counts stay direct-tag only (Phase 22 chose this so the cascade could not inflate counts; rolling up would inflate them symmetrically).
- [ ] Tests: union across a nested tree, excludes own+inherited, ci de-dupe; a gallery matches a deep descendant's tag; leaf gallery empty; `tag_overview` counts unchanged (Phase 22 regression guard); depth bound holds.

**Cross-cutting**
- [ ] Update `CLAUDE.md` if conventions change, ROADMAP index row, and the affected Serena memories (`mem:vault_format` for v9, `mem:module/ui` for the new ui modules, `mem:module/vault` for the tag-description API, `mem:ui_spec` for the tag overview + help popup + tile changes).
- [ ] `scripts/gen.sh` after adding source files so `compile_commands.json` stays accurate.

> **Merge note:** this branch and `phase-52-legacy-video-codecs` each add one row to
> the ROADMAP index directly after Phase 50, so whichever merges second conflicts
> there. Resolution is trivial — keep both rows in numeric order.

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
