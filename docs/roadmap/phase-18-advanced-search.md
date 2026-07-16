## Phase 18 — Advanced search (dedicated screen) ✅

**Goal:** A dedicated search screen for galleries and media with **weighted
tags**, **include/exclude**, **AND/OR-grouped** clauses, **tag autocomplete**,
and **saved, reusable searches** — coexisting with the Phase 12 `/`
quick-overlay.

### Tasks
- [x] **Query model (grouped clauses)** — `src/ui/advanced_search_model.{h,cpp}`: a serialisable query of **include tags** (each with an optional **weight**, default 1, contributing to a relevance **score**), **exclude tags** (hard filter — any match removes the hit), **named groups** of tags each combined **AND**/**OR**, the groups joined by a top-level **AND**/**OR**, plus **gallery-name** substring matching and a **scope** (Images / Galleries / Both). Pure, SDL-free, evaluates a candidate's name + effective tags → `{matched, score}`; headlessly unit-tested (mirrors `search_model`).
- [x] **Tag autocomplete** — `Vault::all_tags()` returns the vault's distinct tag vocabulary (deduplicated case-insensitively across the whole tree); a pure `tag_suggestions(prefix, vocabulary)` helper (case-insensitive, ranked, unit-tested) drives a typeahead dropdown in every include/exclude/group field (`Tab`/`Enter` accept, arrows select).
- [x] **Saved searches in the vault (encrypted)** — extend index serialisation with a **vault-global saved-searches block** (name + serialised query) alongside the tree root; bump **`INDEX_VERSION` 4 → 5** (v1–v4 read back-compat with an empty list); persisted via the crash-safe double-buffer index swap. `Vault` API: `save_search` / `list_saved_searches` / `delete_saved_search` / `run_search(query, scope)`. Enforce count/length bounds so the Phase 7 fuzz suite stays crash-free.
- [x] **UI** — a first-class `Screen` (`NavKind::ToAdvancedSearch`, opened with `Shift+/` from the grid) hosting the clause/group builder, a live result list (reusing grid tile/list rendering), and a saved-searches sidebar (save current / load / delete). Activating a result opens the collection-mode viewer (like favorites) or navigates to a gallery. The Phase 12 `/` overlay is unchanged.
- [x] Update `CLAUDE.md` (new ui module, `Vault::all_tags`/saved-search API, `INDEX_VERSION = 5`) + `mem:core`, and extend the index-format reference for the saved-searches block.
- [x] `tests/` — query evaluation: weighted ranking, exclude filtering, group AND/OR + top-level join, name match, scope; tag-suggestion prefix matching/ranking; `all_tags` dedup across the tree; saved-search round-trip across lock/reopen; a pre-v5 vault opens with no saved searches; the fuzz corpus is extended with a saved-searches block and stays crash-free.

**Out of scope (YAGNI):** freeform query-language parser, regex, date/size-range
filters (candidates for a later phase).

### Acceptance criterion
The advanced-search screen filters and ranks images/galleries by a grouped,
weighted, include/exclude query with working tag autocomplete; searches can be
saved, listed, re-run, and deleted, surviving a lock/reopen; a pre-v5 vault opens
with no saved searches; the extended fuzz suite passes; the `/` quick-overlay
still works.

**Status:** ✅ 458 tests pass under `scripts/test.sh` and `--asan` (ASAN+UBSan+LSan
clean) — 23 new (13 `advanced_search_model` + 4 `index` v5 + 6 vault search; the
fuzz corpus also grew a v5 saved-searches block). The query model
(`src/ui/advanced_search_model.*`) is pure/SDL-
free/vault-free: `evaluate()` returns `{matched, score}` for weighted-include
(OR gate + scorers) ∧ exclude (hard filter) ∧ name-substring ∧ AND/OR groups
(top-level join); `serialize_query`/`deserialize_query` give an opaque saved-
search blob; `tag_suggestions()` ranks prefix-over-substring, deduped. The index
gains a vault-global saved-searches block (`SavedSearch{name, opaque query}`),
`INDEX_VERSION` 4→5 (v1–v4 read back-compat → empty list, bounds-checked against
the fuzz corpus). `Vault` gains `all_tags`, `run_search(AdvancedQuery)` (ranked
by score then path, scope from the query), and `save_search`/`list_saved_searches`/
`delete_saved_search` (upsert by name, persisted via the crash-safe index swap;
carried through `compact()`). `AdvancedSearchScreen` (`Shift+/`, `NavKind::
ToAdvancedSearch`) is a keyboard-driven builder + live results + saved sidebar
(`Ctrl+S` save, `Enter` load/open, `Del` delete); image results open the gallery
viewer, gallery results navigate. The Phase 12 `/` overlay is untouched.
