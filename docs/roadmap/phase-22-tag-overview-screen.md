## Phase 22 — Tag overview screen ✅

**Goal:** A dedicated screen showing every distinct tag in the vault with how
many galleries and images carry it; activating a tag opens a galleries-only view
of the galleries with that tag.

### Tasks
- [x] `VaultSearch::tag_overview()` — walk the decrypted in-memory tree and return, per distinct tag (deduplicated case-insensitively, reusing the `all_tags` vocabulary), a `{tag, gallery_count, image_count}` tally. Counts use **direct tags on each node** (a gallery or image is counted only if it *directly* carries the tag — not the Phase 12 read-time cascade, which would inflate every descendant). No disk access. **Placed on the `VaultSearch` facade rather than `Vault` itself** (the roadmap's original `Vault::tag_overview()`) to keep `Vault` under the cpp:S1448 method cap — same rationale and home as `all_tags`/`run_search`, whose vocabulary it reuses.
- [x] `src/ui/tag_overview_model.{h,cpp}` — pure sort/filter of the overview list (by name or by count; optional typed prefix filter). Unit-tested.
- [x] **UI** — a first-class `Screen` (`NavKind::ToTagOverview`, opened with `Shift+T` from the gallery grid): a scrollable list of `tag — N galleries, M images`, keyboard-navigable, with `Enter` opening a **galleries-only** view of the galleries directly carrying that tag. That view reuses the `favorites_galleries` pattern (a flat list/grid whose activation navigates to the chosen gallery in the normal grid); `Esc` returns to the overview, then to the grid.
- [x] Update `CLAUDE.md` (new ui modules + `VaultSearch::tag_overview`) + `mem:core`.
- [x] `tests/` — `tag_overview` direct-tag counts are correct across a nested tree (a gallery tag does **not** inflate descendant image counts); sort/filter ordering; `Enter` yields exactly the galleries directly carrying the tag; an empty/untagged vault produces an empty overview; counts are stable across a lock/reopen.

### Acceptance criterion
The tag overview lists every distinct tag with its direct gallery and image
counts; selecting a tag opens a view of exactly the galleries carrying it, and
activating one navigates to that gallery; the screen is stable across a
lock/reopen.

**Status:** ✅ 521 tests pass under `scripts/test.sh` and `--asan` (ASAN+UBSan+LSan
clean) — 8 new `tag_overview_model` sort/filter tests + 5 new vault tests
(direct-tag counts with no cascade, galleries-with-tag direct-only, untagged →
empty, locked → empty, stable across lock/reopen). The counting + galleries-only
lookup live on the **`VaultSearch` facade** (`tag_overview()` → `std::vector<ui::TagTally>`
via direct-tag walk, reusing `collect_tags`' vocabulary; `galleries_with_tag()` →
galleries directly carrying a tag) so `Vault` stays under its method cap. The pure
`src/ui/tag_overview_model.{h,cpp}` owns the sort (name / count-desc) + case-insensitive
prefix filter. `TagOverviewScreen` (`Shift+T`, `NavKind::ToTagOverview`) renders the
keyboard-navigable list (Up/Down, Enter, Tab = toggle sort, type = filter); `Enter`
opens `TagGalleries` (`NavKind::ToTagGalleries`, the tag carried in `Nav::path`),
a thin `FavoritesScreen` subclass whose `go_back()` returns to the overview. `image_count`
counts non-gallery media (images + videos) so a video-only tag isn't a phantom 0/0 row.
