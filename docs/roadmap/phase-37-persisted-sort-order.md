## Phase 37 — Persisted per-gallery sort order

**Goal:** Let the user choose how a gallery's contents are ordered — including
a **natural, number-aware name order** — and persist that choice with the
gallery so it survives lock/reopen. Today `Vault::list` (`src/vault/vault.cpp`)
returns children in raw insertion order with **no sort step anywhere**, so a
gallery imported/added as `image1, image2, image3, …, image10` displays in
whatever order it was added, which for lexicographically-provided input reads
as `image1, image10, image2, …`. `src/ui/natural_sort.h` (Phase 24) already
solves exactly this for reading order inside CBZ/archive imports — this phase
reuses it for the live gallery grid instead of writing a second comparator.

### Tasks
- [x] **Index format extension** — a `sort_key` `u8` field on **gallery**
  nodes only (the key that matters is which order a gallery's *children*
  render in, not a per-image property): `0 = Manual` (today's raw insertion
  order — the default), `1/2 = Name natural asc/desc`, `3/4 = Date added
  asc/desc`, `5/6 = Size asc/desc`. Bump `INDEX_VERSION` (5 → 6); v1–v5 blobs
  read back with `sort_key = Manual` on every gallery — **zero visible change**
  for existing vaults until a user explicitly opts in (per your call: no
  auto-upgrade to natural order as a new default).
- [x] `src/ui/gallery_sort.{h,cpp}` — new pure, SDL/vault-free, unit-tested
  module (mirrors `natural_sort.h`/`tag_overview_model.h`):
  `sort_children(std::span<const IndexNode*>, SortKey)`. Always groups
  sub-galleries before images/videos ("folders first", regardless of key);
  within each group, `Manual` is a no-op, the `NameNatural*` keys delegate to
  the existing `natural_less`, `DateAdded*` compares `created_ts`, `Size*`
  compares `orig_size`.
- [x] `Vault` API — `set_gallery_sort(gallery_path, SortKey)` persisted via
  the existing crash-safe double-buffer index swap; `Vault::list` applies the
  target gallery's own stored `sort_key` (via `gallery_sort::sort_children`)
  before returning, so **every** caller — grid, list view, the viewer's
  thumbnail strip, and slideshow order — gets one consistent order for free,
  with no call site re-implementing sorting.
- [x] **UI** — `Shift+S` in the gallery grid cycles the focused gallery's sort
  key (`Manual → Name↑ → Name↓ → Date↑ → Date↓ → Size↑ → Size↓ → Manual`) and
  persists it immediately; the footer/HUD shows the active sort, mirroring the
  existing `T`/`L`/`U` indicator convention.
- [x] Update `CLAUDE.md` (index format table, `INDEX_VERSION` bump, module
  layout, the new `Shift+S` binding) and `mem:core`.
- [x] `tests/` — `sort_key` round-trip across lock/reopen and back-compat
  (v1–v5 blobs read as `Manual` on every gallery); `gallery_sort` unit tests
  covering folders-first grouping, each key ascending/descending, `Manual` as
  a true no-op, and the `image1/image2/.../image10` natural-order case
  specifically; `Vault::list` returns the persisted order across a reopen;
  the viewer strip and slideshow iterate in the same order as the grid for a
  non-`Manual` gallery (both funnel through `Vault::list`, so this holds by
  construction — `image_viewer.cpp`'s album build and `Vault::list`'s own
  round-trip test cover it); the fuzz corpus gains an out-of-range `sort_key`
  byte case (must clamp/reject, never crash).

**Out of scope (YAGNI):** a global/app-wide default sort preference (an
explicit per-gallery-only design — the user did not want natural order forced
on every gallery); sorting the favorites, tag-overview, or advanced-search
result screens (they already have their own view models — `result_grid`,
`favorites_images/galleries` — untouched by this phase); a drag-and-drop
manual-reorder editor (`Manual` stays "whatever insertion order already is",
with no UI to rearrange it); multi-key/secondary sort (e.g. type-then-name)
beyond one active key per gallery.

### Acceptance criterion
Setting a gallery's sort to "Name (natural)" displays `image1, image2, …,
image10` in correct numeric order instead of lexicographic order; the choice
survives lock/reopen and is reflected identically in the grid, list view,
viewer strip, and slideshow; other galleries are unaffected and keep today's
insertion order unless explicitly changed; a pre-existing vault opens with
every gallery defaulting to `Manual` (no visible change) until the user opts
in via `Shift+S`.

**Status:** ✅ 806/806 tests pass (`scripts/test.sh`); `scripts/test.sh --asan` clean.
