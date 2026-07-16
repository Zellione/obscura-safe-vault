## Phase 29 — Tag autosuggest in the tag editor 🔜

**Goal:** While typing a tag in the `G` tag editor (galleries and images alike),
suggest tags that already exist anywhere in the vault. A suggestion can be
selected with the arrow keys — or ignored by simply continuing to type; the
typed text always wins unless a suggestion is explicitly highlighted.

### Tasks
- [x] **Pure model** — new `src/ui/tag_suggest.{h,cpp}` (test-target unit like `tag_inherit`): `editor_tag_suggestions(buffer, vocabulary, own_tags)` → trims the buffer, delegates ranking to the Phase 18 `ui::tag_suggestions` (prefix matches first, then substring, ci de-dupe), filters out tags the node already carries (`tag_ci_equal` — suggesting them would be a no-op merge), caps at 5. Cursor movement reuses the tested `ui::move_tag_cursor` (−1 = editing the buffer).
- [x] **Vocabulary** — `VaultSearch::all_tags()` fetched in `TagEditor::open()` and re-fetched after each successful add/remove, so a just-created tag is immediately suggestible.
- [x] **TagEditor wiring** — buffer empty ⇒ behaviour unchanged (Up/Down scroll own tags, Del removes). Buffer non-empty ⇒ a dropdown of ≤5 suggestions under the input box; Up/Down move the suggestion highlight; **Enter adds the typed text** unless a suggestion is highlighted (then it adds the suggestion); **Esc deselects** a highlighted suggestion instead of closing (nothing highlighted ⇒ closes as today); typing/backspace recomputes suggestions and clears the highlight. The dropdown draws *over* the tags-list area (combobox style, drawn last); the fixed modal does not resize.
- [x] Update `CLAUDE.md` (tag_editor + new module entry) + `mem:core`.
- [x] `tests/` — unit tests for `editor_tag_suggestions`: ranking passthrough, own-tag exclusion (ci), empty buffer → empty, cap at 5, first-casing de-dupe.

**Out of scope (YAGNI):** changing the advanced-search screen's existing autocomplete; fuzzy matching beyond the Phase 18 prefix/substring ranking; suggestion counts/frequency ranking; a shared combobox widget.

### Acceptance criterion
Typing in the tag editor of a gallery or image shows up to 5 existing vault tags
matching the typed text; Down+Enter adds the highlighted suggestion, plain Enter
adds exactly what was typed, and tags already on the node are never suggested.

**Status:** ✅ 691/691 tests pass; `scripts/test.sh --asan` clean. Pure `ui::editor_tag_suggestions` (tag_suggest.{h,cpp}) over the Phase 18 ranking + `move_tag_cursor`; TagEditor overlays a ≤5-row dropdown while typing — Enter adds the typed text unless a suggestion is highlighted, Esc deselects before it closes.
