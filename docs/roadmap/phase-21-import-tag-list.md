## Phase 21 — Import a tag list onto a gallery ✅

**Goal:** Bulk-add tags to a gallery from a plain-text file (one tag per line),
so large tag sets don't have to be typed one at a time in the tag editor.

### Tasks
- [x] `src/ui/tag_list_parse.{h,cpp}` — pure parser: raw file bytes → normalised tag list. Splits on LF/CRLF, trims surrounding whitespace, drops blank lines, de-duplicates **case-insensitively**, caps the count at `INDEX_MAX_TAGS` and each tag at the u16 length bound. Unit-tested.
- [x] **UI entry point** — `Shift+G` in the gallery grid (with a focused gallery tile) opens a `.txt` file dialog. Reuse the `platform::file_dialog` async pattern **with a `Purpose` tag** (Phase 17 fix) so its result can't be stolen by another in-flight dialog (e.g. image import / zip import).
- [x] **Apply** — read the file via `platform::read_file`, parse, then `Vault::add_tag` each tag onto the focused gallery node (merging with its existing tags), persisted via the crash-safe double-buffer index swap; refresh and show a post-import summary (added / skipped counts). The text file carries tag **metadata** (like filenames) — not key material and not decrypted vault content — so reading it is consistent with the security invariants.
- [x] `tests/` — parser edge cases (CRLF, blank lines, duplicates, surrounding whitespace, count/length bounds, non-UTF-8 bytes handled without crash); a fixture list applied to a gallery survives a lock/reopen and merges (not replaces) existing tags; the `file_dialog` `Purpose` isolation holds.

### Acceptance criterion
Selecting a gallery, choosing a `.txt` file of one-tag-per-line, imports the
de-duplicated tags onto that gallery; they merge with any existing tags, survive
a lock/reopen, and a summary reports the added/skipped counts.

**Status:** ✅ 501 tests pass under `scripts/test.sh` and `--asan` (ASAN+UBSan+LSan
clean) — 10 new `tag_list_parse` parser tests, 2 new `file_dialog` `Purpose`-isolation
tests, and a vault merge/reopen integration test. The parser is the pure, SDL/vault-free
`src/ui/tag_list_parse.{h,cpp}` (`parse_tag_list(span<const uint8_t>)` → splits on LF,
trims a trailing CR + surrounding whitespace, drops blanks, de-dupes case-insensitively
keeping first casing, truncates each tag to `TAG_MAX_BYTES` = 0xFFFF, caps at
`INDEX_MAX_TAGS`; bytes are opaque so non-UTF-8 input never crashes). `FileDialog` gained
`Purpose::TagList` + `open_tag_list()` (filtered to `.txt`). In `GalleryGrid`, `Shift+G`
on a focused **gallery** tile stashes the target path and opens the dialog; `update()`
drains the `TagList` result, reads the file (`platform::read_file`), parses, and applies
each tag via a free `apply_tag_list()` helper (counts added/skipped by the node's
tag-count delta — no UI-side case-folding), showing a `"Tag import: N added, M skipped"`
summary. Both the `Shift+G` entry and the result pump are inlined (no new `GalleryGrid`
methods) to stay under the S1448 method cap, mirroring the Phase 17 `Z` zip-import inline.
