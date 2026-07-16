## Phase 12 — Tags & Search ✅

**Goal:** Per-node tags on **both images and galleries**, with gallery tags
cascading to descendants, and a scoped search across the whole vault.

### Tasks
- [x] **Index format extension** — add a tag list (`u16 count` + length-prefixed UTF-8 strings) to **both** gallery and image nodes; bump `INDEX_VERSION` (1 → 2). Deserialisation reads pre-tags vaults as having empty tag lists (back-compat). Enforce count/length bounds (`INDEX_MAX_TAGS = 4096`, per-tag length u16) so the Phase 7 fuzz suite stays crash-free.
- [x] **Cascade (read-time)** — a node's *effective tags* = its own tags ∪ the tags of all ancestor galleries, computed on the fly during traversal/search. Gallery tags are never copied onto descendants, so editing or removing a gallery tag stays consistent automatically.
- [x] `Vault` API — `set_tags(node_path, tags)` / `add_tag` / `remove_tag` for both node kinds, persisted via the existing crash-safe double-buffer index swap; `search(query, scope)` where `scope ∈ {Images, Galleries, Both}` walks the decrypted in-memory tree and matches `name` + effective tags (case-insensitive substring). No OCR, no disk access.
- [x] `src/ui/search_model.{h,cpp}` — pure query tokenisation + match/rank against name and effective tags; unit-tested.
- [x] **UI** — `/` opens a search overlay (`src/ui/search_overlay.*`) with a live-filtered result list and an **Images / Galleries / Both** scope toggle (`Tab`); a tag-editor widget (`src/ui/tag_editor.*`, add/remove tags via `G`) reachable from the viewer and from a gallery tile.
- [x] Update `CLAUDE.md` (index node now carries tags; `INDEX_VERSION` bump) and the relevant Serena `mem:core` memory.
- [x] `tests/` — tag round-trip across lock/reopen for images **and** galleries; a gallery tag is reported in a descendant image's effective tags; search scope correctly returns only images / only galleries / both; case-insensitive matching; a pre-tags vault opens with empty tags; the fuzz corpus is extended to tagged gallery + image nodes and stays crash-free.

### Acceptance criterion
Tags added to images and galleries survive a lock/reopen; a gallery's tags are
inherited by its descendant images; scoped search returns the correct set of
images or galleries across the whole tree; a pre-tags vault opens cleanly with no
tags; the extended fuzz suite passes.

**Status:** ✅ 252/252 tests pass under `scripts/test.sh` and `--asan` (50 new:
index tag round-trip/back-compat/bounds, 26 vault tag/cascade/scope/search,
23 `search_model` tokenize/match/rank). The index format bumped to
`INDEX_VERSION = 2` and reads v1 blobs back-compatibly with empty tag lists;
the fuzz corpus now seeds tagged gallery + image nodes. Tags + scoped search
live in the vault layer (`set_tags`/`add_tag`/`remove_tag`/`search`, cascade at
read time); the pure `ui::search_model` drives the overlay's live filter/rank.

> **Notes / decisions made during implementation**
> - **Uniform tag block.** Tags serialise the same way for both node kinds —
>   written immediately after the node `name`, before the gallery-children /
>   image-meta branch — so the reader parses them version-gated (`version >= 2`)
>   without branching on node type.
> - **Cascade is read-only.** Effective tags are computed during the search
>   DFS (own ∪ inherited, case-insensitively de-duplicated); the root gallery's
>   tags are global. Gallery tags are never copied onto descendants, so a tag
>   edit is instantly consistent everywhere.
> - **Two matchers, clean boundaries.** `Vault::search` does cascade + a small
>   local case-insensitive substring match (vault can't depend on `ui`); the
>   richer multi-token AND-match + ranking lives in the pure `ui::search_model`,
>   which the overlay applies on top of the in-scope candidate set
>   (`vault.search("", scope)`).
> - **Overlays, not screens.** The search overlay and tag editor are modal modes
>   inside `GalleryGrid` / `ImageViewer` (mirroring `consent_dialog` /
>   `export_ui`), so no new `NavKind`/`Screen` was needed. Keys: `/` search,
>   `G` tag editor, `Tab` cycles search scope, `Esc` closes.
