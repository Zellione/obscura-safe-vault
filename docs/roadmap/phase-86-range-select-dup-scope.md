# Phase 86 — Range selection & scoped duplicate finder

## Overview

Two owner-requested features, both extensions of existing flows:

1. **Range selection** — select one item, select (or focus) another, press
   `Shift+Space`, and everything between is selected too. Works on every
   surface that already has the Phase 68 multi-select convention.
2. **Duplicate-finder scope** — the Phase 61 duplicate finder (`Ctrl+D`) can
   now scan *the browsed gallery only*, or *the browsed gallery against the
   whole vault*, in addition to the historical whole-vault scan.

---

## 86.1 — Shift+Space range selection

### Model (`ui/selection_model.*`, pure, unit-tested)

`SelectionModel` now tracks a **range anchor**: the most recent index toggled
ON with Space, plus the one before it.

- `range_for(focus)` returns the span a range fill should select, or nullopt:
  - anchor exists and differs from the focus → `{anchor, focus}` — the
    file-manager "anchor then extend" flow;
  - the focus sits ON the anchor and a previous toggled-on index exists →
    `{previous, focus}` — the literal "select A, select B, press the key"
    flow the owner described;
  - otherwise nullopt (nothing to span).
- `select_range(a, b)` selects the inclusive span in either order, one
  revision bump, without moving the anchor — extending again re-spans from the
  same item.
- `select(i)` selects without ever deselecting and **without** establishing an
  anchor — the gallery grid's filtered fill adds items one by one and must not
  move the anchor the way a hand toggle does.
- Anchor maintenance: toggling OFF the anchor demotes the role to the previous
  toggled-on index; `clear()` resets both; `select_all` (Ctrl+A) deliberately
  never establishes an anchor (it is not a hand-picked selection).

### Hosts

`Shift+Space` is wired everywhere plain Space already toggles:

- **Gallery grid** — `GalleryGrid::select_range_to_focus()` clamps the span to
  the listing and selects only what Space would accept (`is_selectable`),
  skipping the rest without breaking the fill. With no anchor, the footer
  explains the flow instead of doing nothing silently.
- **Favorites / tag screens** (`FavoritesScreen` base) and the
  **advanced-search result panel** (`SearchResultView`) — every row is
  selectable, so the span goes straight through `select_range`, clamped to the
  listing.

F1 help on all three surfaces gains a `Shift+Space — Select range` entry.

---

## 86.2 — Duplicate-finder scope

### Scope enum + pure helpers

- `ui::DupScope { WholeVault, GalleryOnly, GalleryVsVault }` (`dup_model.h`).
- `dup_scope_label(scope, gallery_path)` — the one label used by the chooser
  and the review header, so they cannot drift.
- `scope_filter_groups(groups, gallery_path)` (pure, unit-tested) — drops
  every group with **no** member inside the gallery subtree. Component-boundary
  safe: gallery `a` never matches `ab/x.png`. Surviving groups keep ALL their
  members — the outside copies stay reviewable, so the user can remove either
  side of the duplication. Empty gallery path is a no-op.
- `collect_scan_items(v, scope_path)` — the existing collector gains a scope
  parameter (default empty = whole vault, all existing call sites unchanged);
  a non-empty scope walks only that gallery's subtree.

### Screen wiring (`duplicates_screen.*`)

The Choose state gains a **Scope** line under the two mode rows, shown only
when the finder was opened from inside a non-root gallery — the screen already
carries the browsed gallery path in its back-navigation Nav (`back_.path`,
set by `App::to_duplicates` from the grid or the active dual pane), so no new
plumbing was needed. `Left`/`Right` cycles the three scopes; `Up`/`Down` still
picks the mode; `Enter` starts the scan with both.

- **Whole vault** — unchanged historical behavior, and the only scope at root.
- **This gallery only** — `collect_scan_items(vault, back_.path)`: hashes just
  the subtree, so the scan is proportionally cheaper.
- **This gallery vs whole vault** — hashes the whole vault (a match can live
  anywhere), then `finish_scan` runs `scope_filter_groups` before the review
  is built. Deliberately costs the same as a whole-vault scan — an honest
  full pass beats a size-prefilter shortcut that would miss perceptual and
  video matches.

The review header appends the scope label when it is not WholeVault. Waves,
marking, apply, inspect, and the stale banner are completely unchanged; both
scopes work in both scan modes (exact / exact + visually similar, videos
included). Rescanning from Done keeps the chosen scope.

### Recursion decision

"This gallery" is **recursive** (the whole subtree), matching how Del, export
tallies, and the detail panel treat a gallery — not Ctrl+A's direct-children
rule, which exists to match what is visible on screen.

---

## Tests

- `tests/ui/test_selection_model.cpp` — 12 new cases: anchor extend, fill
  between the two most recent selections, lone-anchor/no-anchor nullopts,
  anchor demotion on deselect, anchor survival across a fill, clear/select_all
  anchor rules, inclusive both-order spans, single revision bump, anchor-free
  `select`.
- `tests/ui/test_dup_scan.cpp` — scoped collection: subtree-only items with a
  name-prefix decoy gallery (`a` vs `ab`), empty scope = whole vault.
- `tests/ui/test_dup_model.cpp` — scope filter: keeps groups touching the
  gallery (members intact), drops outside groups, component boundaries,
  nested descendants, empty-scope no-op; the three scope labels.

2120 tests / 0 failed.

## No format change

No `.osv` change; `INDEX_VERSION` unchanged. Both features are session-only
UI/model work.
