# Tag category templates & non-blocking tagging (Phase 73)

**Status:** ✅ shipped
**Date:** 2026-08-10

## Problem

Two concurrent UX gaps exist:

1. **Template fields.** Tag categories are bare names with no structure. A user might tag
   an image with `artist:bob` but have no way to associate additional facts (bob's
   country, the album) with the tag. Fields are entered manually each time, duplicating
   work.

2. **Non-blocking tagging.** Tagging a selection of 50+ items freezes the UI. Each
   `add_tag` on each node runs a full `commit_index()` (whole-tree serialize + fsync)
   on the UI thread; a 50-item multi-add is 50 commits, a ~500 ms stall on modest
   hardware.

## Goal

Two deliverables in one phase:

1. **Tag category templates.** A tag category may optionally define a template — an
   ordered list of named fields. When a *brand-new* tag of a templated category is
   added, the user fills the fields (skippable). Field values are attached to the
   tag (not the node) and behave like extra tags for search and filtering. Previously
   entered values for the same (category, field) pair are offered as dropdown suggestions.

2. **Non-blocking tagging.** Tagging a selection of 50+ items must not freeze the
   UI. Tags batch into one commit and the commit's write/fsync move off the UI thread
   via a persistent CommitLane.

## Decisions made with the owner

- Field values match **items carrying the tag**: searching a value finds every
  image/gallery tagged with the owning tag (values act like extra tags in plain
  search and the advanced-search builder alike).
- The fields sheet appears **only for brand-new tags** (tag not yet in
  `VaultSearch::all_tags`). Re-using an existing tag adds it silently. Fields are
  editable later from the tag overview.
- **Single value per field** per tag.
- Template CRUD lives on the **tag overview screen** (`Shift+T`).
- **Templates are optional** — a category with no template behaves exactly as
  today (no prompt, no change).
- Dropdown suggestions are scoped **per (category, field)** — no cross-category
  pooling.
- Non-blocking fix is **fully async**: batch mutation + ONE commit, and the
  commit's write/fsync moved off the UI thread via a persistent CommitLane.

---

## 1. Data model (`src/vault/index.*`) — INDEX_VERSION 10 → 11

### Category template fields

`TagCategory` gains `std::vector<std::string> fields` — the template, ordered,
empty = no template. Serialized in the v11-gated arm of `read_settings`/`write_settings`
after each category's swatch byte, as a `u8 field_count` followed by field entries
`{ u16 field_name_len; u8[field_name_len] }`.

### Vault-global tag-field-values block

New block serialized after the Phase 65 migration watermark: a flat list of
`TagFieldValue{std::string tag; std::string field; std::string value;}` entries.

- Single value per (tag, field); tag and field matched case-insensitively via
  `ui::tag_ci_equal` (first-seen casing kept, matching `add_tag` / `TagDescription`
  conventions).
- Setting an empty value removes the entry (mirrors `set_tag_description`).
- Serialized as `u16 entry_count` followed by entries
  `{ u16 tag_len; u8[tag_len]; u16 field_len; u8[field_len]; u16 value_len; u8[value_len] }`.

### Capacity caps

Writer CLAMPS, reader REJECTS — the existing block contract:

- `INDEX_MAX_TEMPLATE_FIELDS = 16` fields per category
- `INDEX_MAX_FIELD_BYTES = 64` per field name
- `INDEX_MAX_FIELD_VALUE_BYTES = 256` per value
- `INDEX_MAX_TAG_FIELD_VALUES = 16384` total entries

Out-of-range bytes are rejected on deserialise, not clamped.

### Back-compat

Pre-v11 blobs read as empty `fields` + empty value list. The category block's
per-entry layout is extended in a v11-gated arm of `read_settings`/`write_settings`
(the `version` parameter already exists).

### API surface

Free functions over `VaultSettings` (keeping `Vault` under its `cpp:S1448` cap,
mirroring `find_tag_description`/`set_tag_description`):

- `vault::category_template(settings, category_name) -> span/vector of fields`
- `vault::set_category_template(settings, category_name, fields)`
- `vault::find_tag_field_value(settings, tag, field) -> optional`
- `vault::set_tag_field_value(settings, tag, field, value)` (empty value erases)
- `vault::rename_template_field(settings, category, old_field, new_field)` —
  re-keys stored values; `remove` deletes the field's values.

Persistence rides the existing `set_vault_settings` crash-safe commit.

---

## 2. Search — virtual-tag match rule

At search start, build ONE case-insensitive map: tag → its virtual tags, where
each stored `TagFieldValue{tag, field, value}` yields the virtual tag string
`"field:value"`. Built from `settings_` only; O(stored values).

A node's effective tag set *for matching* becomes:
own tags ∪ inherited (existing cascade) ∪ virtual tags of every carried tag
(own + inherited) that has stored values.

### Plain and advanced search

- Plain search: substring matching over `"field:value"` means both `"Japan"`
  (substring) and `"country:Japan"` (full) hit carriers of `artist:bob` when
  bob's country = Japan.
- Advanced-search tag groups, excludes, and scorers see virtual tags exactly like
  real tags.

### Match-only visibility

Virtual tags are **match-only**. They never appear on tiles, chips, the tag
editor lists, tag-overview tallies, `all_tags` vocab, or the Phase 51 subtree
tag-union roll-up shown for gallery hits. (Implementation may thread them
through the DFS, but every *display* surface filters them out — simplest is to
expand only inside the match predicate, never into returned collections.)

---

## 3. Tagging UX

### New-tag fields sheet

`TagEditor::add_chosen_tag`: after resolving the chosen tag, if

- (a) it is not present in the current `all_tags` vocabulary (case-insensitive),
- (b) `ui::resolve_tag` maps its prefix to a configured category, and
- (c) that category's template is non-empty,

then apply the tag immediately (batch API, as today) and open the **fields sheet**
right after — the sheet only collects values, it never gates the add.

**Fields sheet behavior:**

- Modal sequence of one input row per template field, in template order.
- Each row shows a dropdown of suggestions — distinct stored values for that
  (category, field) across all tags, ranked by the same prefix-match-then-substring
  logic as `editor_tag_suggestions`.
- New pure helper `ui::field_value_suggestions(buffer, category, field, settings)`,
  unit-tested, capped at `TAG_SUGGEST_MAX`.
- Up/Down highlight, Enter accepts row and advances, typing filters; Esc skips the
  remaining fields.
- **The tag is added regardless** — skipping just leaves fields empty. Values are
  persisted via one `set_vault_settings` at sheet completion.

### Multi-select

Multi-select (`open_multi`) behaves identically: the sheet appears once (the
values belong to the tag, not the nodes).

---

## 4. Tag overview (`src/ui/tag_overview.*`)

### Template CRUD

A new keybind (chosen at implementation time to avoid the screen's existing bindings;
candidate `Ctrl+T`) opens a category picker listing configured categories, then a
template editor:

- Add field
- Rename field (re-keys stored values)
- Remove field (confirm modal — deletes that field's stored values)
- Reorder not supported this phase

Uses the screen's existing prompt/list machinery (`prompt_layout.*` / `list_layout.*`
conventions).

### Per-tag field editing

The Phase 51 edit prompt (currently description-only) gains one row per template
field when the tag's category has a template, with the same suggestion dropdowns.
This is where skipped fields get filled later.

### Help text

`F1` help text updated for both template CRUD and per-tag field editing.

---

## 5. Non-blocking tagging

Two layers; both land in this phase.

### 5a. Batch tag APIs (`src/vault/`)

- `vault::add_tag_batch(v, span<const std::string> node_paths, tag)` and
  `vault::remove_tag_batch(v, span<const std::string> node_paths, tag)` — free
  friends beside `set_favorites_batch`, same contract: main-thread only, skip
  non-resolving paths, mutate every target, then ONE `commit_index()` — and none
  when nothing changed. `Locked` if locked; `IoError` if the commit fails.
- `TagEditor::add_chosen_tag` / `remove_selected_tag` multi-apply loops switch to
  the batch calls.

### 5b. Persistent CommitLane (app-owned)

The App installs a started `CommitLane` as the vault's commit router at unlock
and it stays installed until lock. Every interactive `commit_index()` (tags,
favorites, rename, sort, settings, deletes…) becomes serialize-on-UI-thread +
async write/fsync on the lane thread.

**Lane lifecycle:**

- App owns the lane: installs at vault unlock, uninstalls at vault lock.
- `ImportQueue` stops creating its own lane: `begin_session` asserts/reuses the
  installed router; `end_session` keeps its enqueue-final-snapshot + flush but no
  longer stops/uninstalls the lane.
- Existing invariants already hold and are re-verified by tests:
  - `Vault::lock()` auto-stops the lane before key wipe (quiesce-before-wipe).
  - `compact()` / `reclaim()` flush the router before owning the write path.
  - Lane write failure is a hard stop surfaced on the import status page; this
    phase extends that surface to a generic "vault write error" the grid shows
    even outside an import session (exact surface chosen at implementation; the
    failure must not be silent).

---

## 6. Durability trade-off

A crash can lose the last ~2 s of *interactive* metadata commits (lane batching
N=32/2 s; flush-on-drain keeps the window small in practice). Media data itself is
unaffected (chunks are fflushed at stage time; same orphan-reclaim story as imports).
Accepted by the owner as part of "fully async".

---

## 7. Testing

TDD; all pure helpers unit-tested.

### Format tests

- v11 round-trip (templates + values), v10-blob back-compat (empty).
- Capacity clamp/reject pairs.
- Fuzz-harness reachability: the base blob must serialize with the 4-arg form and
  include a templated category + field values so the new bytes are reachable by
  mutation.

### Search tests

- Virtual-tag expansion in plain + advanced search.
- Inherited-tag carriers match too.
- Excludes work on virtual tags.
- Virtual tags absent from tallies/vocab/roll-ups.

### Suggestion tests

- `field_value_suggestions` ranking, (category, field) scoping, dedup, cap.

### Batch API tests

- Multi-node add/remove with ONE commit (sync-count pinned via `fileutil::sync_call_count`,
  the `test_transfer_batching` convention).
- No-change → no commit; skip-missing.

### Lane tests

- Interactive commit routes through an installed lane.
- Import session reuses it.
- Flush on lock.
- Failure surfaces.

### ASAN pass

`scripts/test.sh --asan` — vault/memory changes.

### TSan pass

`scripts/test.sh --tsan` — lane lifetime change.

---

## Acceptance criterion

A category given a 2-field template prompts a skippable pre-filled-dropdown
fields sheet exactly when a brand-new tag of that category is added; searching a
stored field value (bare or `field:value`) finds every carrier of the owning
tag; and adding a tag to a 50+ item selection keeps the UI responsive (one
commit, fsync off the UI thread), verified by the pinned sync-count test.
