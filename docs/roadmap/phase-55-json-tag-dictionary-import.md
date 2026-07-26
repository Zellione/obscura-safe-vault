## Phase 55 — JSON tag dictionary import ✅

**Goal:** Import a JSON file containing tag categories and descriptions into the
vault's tag vocabulary, so a shared or reference dictionary can seed the entire tag
system without manual entry. The import populates the vault-global settings block
only — it does not tag any content.

**User-facing file format reference:** [`docs/TAG_DICTIONARY_JSON.md`](../TAG_DICTIONARY_JSON.md).

Full design, including recorded tradeoffs:
[`docs/superpowers/specs/2026-07-25-phase55-json-tag-dictionary-import-design.md`](../superpowers/specs/2026-07-25-phase55-json-tag-dictionary-import-design.md).

### Tasks

**1. Parser — `src/ui/tag_json_parse.{h,cpp}`**
- [x] Exception-free nlohmann/json parser: raw JSON bytes → `TagDictEntry` vector. Follows the same pattern as `src/ui/meta_json.cpp` (`json::parse(begin, end, nullptr, false)` + guards, no `try`/`catch`).
- [x] Tolerates bare array or object with `"tags"` key. Each entry has required `name`, optional `category` and `description`.
- [x] Rules: trim all fields; truncate descriptions to `INDEX_MAX_TAG_DESC_BYTES` (512) at a UTF-8 boundary; reject `name` if it contains a colon (counted as malformed, not silent); case-insensitive de-dupe within one file keeping first casing (matches `parse_tag_list` and `Vault::add_tag`); malformed entries skipped, rest continues.
- [x] Returns `TagDictParseResult{ entries[], malformed_skipped count }`.
- [x] Pure, no vault, no SDL, no disk.
- [x] Tests: bare array and object wrapper; all fields present, some omitted, all empty; trim on all fields; colon-in-name rejection; UTF-8 boundary truncation (including mid-sequence); case-insensitive de-dupe; malformed entries non-fatal; field-size and cap bounds (`INDEX_MAX_TAG_CATEGORIES`, `INDEX_MAX_TAG_DESCRIPTIONS`); non-UTF-8 bytes handled without crash.

**2. Applier — `src/ui/tag_dict_import.{h,cpp}`**
- [x] Pure function `apply_tag_dict(vault::VaultSettings&, entries[]) → TagDictImportSummary` — it mutates a settings struct only, so it is unit-testable with no vault, no disk and no SDL. The **caller** does the one `vault::set_vault_settings()` commit; keeping the commit out of this function is what keeps it pure.
- [x] New categories appended with auto-assigned swatch from the 16-colour palette, wrapping round-robin after 16. Existing categories keep their colour.
- [x] Descriptions upserted via `vault::set_tag_description`; **empty description leaves existing description intact** (deliberate Phase 51 divergence — tested).
- [x] The tag-overview caller performs a single `vault::set_vault_settings()` commit after `apply_tag_dict` returns — one crash-safe index write for the whole file, never one per entry.
- [x] Summary: `{ categories_added, descriptions_added, descriptions_updated, entries_skipped_malformed, entries_skipped_over_cap }`.
- [x] Tests: new category auto-assigned swatch, existing category colour unchanged, swatch wrap-around; empty description preserves existing (vs. Phase 51 edit-time removal); vault survives lock/reopen with applied settings.

**3. UI — entry point and flow**
- [x] `platform::FileDialog::Purpose::TagJson` + `platform::open_tag_json()`, filtered to `*.json` plus all-files, mirroring `open_tag_list()`. Purpose-tagged so it is not drained by another poller (Phase 51 precedent).
- [x] `TagOverviewScreen`: `Ctrl+I` opens the file dialog (modifier chord required — plain letters go into the filter field per `src/ui/tag_overview.cpp:241-244`).
- [x] `update()` drains the `TagJson` result, reads file, parses, applies, shows summary modal, reloads the overview.
- [x] Summary modal reports all five counts from `TagDictImportSummary`.
- [x] `TagOverviewScreen::help_groups()` adds `{"Ctrl+I", "Import tag dictionary"}` to the Navigate group.
- [x] Tests: dialog Purpose isolation (not drained by export or other handlers); multi-entry result correct; summary modal counts match applied counts; overview shows new descriptions and categories immediately after import.

**4. Documentation**
- [x] User-facing JSON format reference: complete worked example (all fields), minimal example (required fields only), semantics when category/description are absent or empty, constraint on colon in name. **Written to `docs/TAG_DICTIONARY_JSON.md`, not to the spec doc** — `docs/superpowers/` is gitignored, so a user-facing reference placed there would never ship. Linked from the README and from this file.

**Cross-cutting**
- [x] Update `ROADMAP.md` index row, adding Phase 55 in numeric sequence.
- [x] `scripts/gen.sh` after adding `tag_json_parse.cpp` and `tag_dict_import.cpp` so `compile_commands.json` stays accurate.
- [x] Update Serena memories: `mem:module/ui` (new modules `tag_json_parse`, `tag_dict_import`); `mem:ui_spec` (tag overview `Ctrl+I` entry point and summary modal); `mem:vault_format` **explicitly NOT changed** (no `INDEX_VERSION` bump; Phase 49 and Phase 51 blocks reused as-is).

### Acceptance criterion

Selecting a `.json` file of tag dictionaries on the tag overview screen via `Ctrl+I`
imports the categories and descriptions into the vault's settings block; new categories
appear with distinct colours from the 16-swatch palette, and new descriptions are
visible in the tag overview immediately. The summary modal reports how many categories
and descriptions were added, updated, or skipped. An invalid or incomplete entry is
skipped gracefully, and the import continues. Importing an empty description field
leaves any existing description intact. Entries with a name containing a colon are
rejected. The imported vault survives a lock/reopen with all settings intact. All tests
pass under `scripts/test.sh` and `--asan`.

### Deviations from the plan (decided during delivery)

- **Truncation is its own count, not a "skip".** The design said an over-long
  `category`/`description` should be truncated *and* counted in `malformed_skipped` —
  but such an entry IS imported, so counting it as skipped would misreport it. The
  owner chose a separate `fields_truncated` counter: `malformed_skipped` now means
  exactly "entry dropped", and the modal reports shortening on its own line.
- **`entries_skipped_over_cap` is split in two.** A description that has no room
  (`INDEX_MAX_TAG_DESCRIPTIONS` full) and a category that has no room
  (`INDEX_MAX_TAG_CATEGORIES` full) are different outcomes: the first loses the
  description, the second only loses the colour registration while the description
  still lands. They get `entries_skipped_over_cap` and `categories_skipped_over_cap`
  so neither cap is reported as the other.
- **`apply_tag_dict` takes the whole `TagDictParseResult`, not a bare entry span.**
  It is still pure over `VaultSettings` and still touches no vault; taking the result
  lets it carry the parse-side counts into the one summary the modal renders, instead
  of the caller stitching two structs together.
- **The entry-count cap lives in the parser, the category-count cap in the applier.**
  `INDEX_MAX_TAG_DESCRIPTIONS` bounds how many entries one file may contribute (a DoS
  bound on the file, mirroring `parse_tag_list`); `INDEX_MAX_TAG_CATEGORIES` can only
  be checked against what the vault already holds, so it belongs to the applier.
- **Summary lines are a pure function** (`tag_dict_summary_lines`) so the wording and
  the counts are testable together; the modal only draws them.

### Known limit

The tag overview lists tags that something in the vault **carries**. A description
imported for a tag nothing carries yet is stored in the settings block, survives
lock/reopen, and appears the moment that tag is first used — but until then it has no
row on the overview. That follows directly from this phase being dictionary-only: it
deliberately tags no content. `tag_dict_import_description_shows_on_the_overview_for_a_tag_in_use`
pins both halves of that behaviour.

**Status:** ✅ Shipped — 1556 tests / 0 failed, `--asan` clean.
