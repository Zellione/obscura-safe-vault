# Pull transfers, tag persistence, 512px thumbnails (Phase 75)

**Status:** ✅ shipped
**Date:** 2026-08-11

## Problem

Three long-standing gaps, delivered as one phase (PR #183):

1. **Transfers were push-only.** Moving content between vaults meant unlocking the
   *source*, selecting there, and pushing to a destination. "I'm in my main vault and
   want to grab two galleries from another vault" required switching vaults entirely.
2. **Transfers silently dropped tags and favorites.** `copy_one_media` and the staging
   attach API carried data, timestamps, and probe metadata — but not `IndexNode::tags`
   or `favorite`. Gallery recreation via `ensure_gallery_path` lost gallery own-tags
   too (only `combine_galleries` unioned them). Worse, an item that *inherited* tags
   from its ancestor galleries lost them the moment it left that ancestry.
3. **Stored thumbnails were 256 px** while the largest grid tile was already 320 px —
   and the owner wanted all four densities bigger still. Bigger tiles on a 256 px
   budget means visible upscaling blur.

## What shipped

### Pull transfers ("From another vault")

- `M` (TransferDialog) opens with a new **Direction** stage when the host provides a
  browse context (`set_current_gallery`, called by the gallery grid before every
  `open*`): *To another vault* (the push flow, unchanged) or *From another vault*.
- Pull flow: **Mode (Move/Copy) → PickingDest relabeled "Source vault:"
  (`VaultUnlockPicker::open` gains `include_self = false` — no "This vault" row;
  the Phase 66 warm slot works symmetrically as a password-free source) →
  PickSrcGalleries → Conflict → Running.**
- **PickSrcGalleries:** `GalleryPickerModel` gains opt-in multi-select
  (`set_multi/toggle_checked/is_checked/checked`; Space toggles, `/` filter, checked
  items returned in list order; `set_items` clears the mode so the two existing
  single-select drivers are untouched). The list comes from new
  `vault::all_galleries` (every gallery except root, DFS order). On Enter,
  `ui::drop_descendant_paths` absorbs a checked descendant into its checked ancestor.
- Conflict handling reuses the Phase 71 stage verbatim (`colliding_galleries`
  pre-scan against the browsed gallery; Combine / Rename `_2` / Cancel).
- Launch is `FileOpJob::start_transfer_galleries` with src/dst roles swapped: source
  = the picker-owned transient vault, destination = the active vault at the browsed
  gallery. The target is threaded explicitly through `launch_current(target, policy)`
  in all four paths (push/pull × conflict/no-conflict). Completion status reads
  "… **from** <vault>"; push wording unchanged.
- Collection screens (favorites/tags/search) never call `set_current_gallery`, so
  they keep today's push-only dialog. Media directly in the source vault's root is
  not pullable this phase (documented limitation).

### Tags + favorites persist on every transfer

- New `vault::NodeExtras{tags, favorite}` parameter (trailing, defaulted) on
  `attach_image_prestaged` / `attach_video_prestaged` / `add_*_prestaged`. Extras are
  written onto the node *before* tree insertion, so they ride the Phase 69 batched
  commits — zero extra fsyncs (`test_transfer_batching` budgets pinned unchanged).
- `vault::tag_ci_equal` (moved out of vault.cpp's file-local `ci_equal` into
  index.h/.cpp) is now the single vault-side tag-identity definition;
  `vault::effective_tags(v, node_path)` computes own ∪ root ∪ ancestor-gallery tags,
  own casing first, ci-deduped.
- **Materialization rule (Move AND Copy): what you saw is what travels.** Directly
  transferred media carry their source-side effective tags; a transferred gallery
  subtree's ROOT gains the tags it inherited from ancestors above it (as plain own
  tags), while everything inside keeps only its own tags — the internal inheritance
  chain travels intact. `GallerySnap` carries each gallery's own tags + favorite;
  `apply_gallery_extras` unions them onto recreated/pre-existing destination
  galleries (ci-deduped, capped at `INDEX_MAX_TAGS`, favorite ORed) — idempotent on
  rerun. Combine-moved media get extras like any other transfer; combine's existing
  gallery-tag union is preserved.

### 512 px thumbnails, bigger tiles, migration

- `image::THUMB_MAX_SIDE = 512` replaces the three hard-coded 256s
  (`vault/staging.cpp`, `ui/import_queue.cpp`, `media/video_probe.cpp` poster).
- Tile sizes (`ui::cell_size_for`): S 128→192, M 188→256, L 248→352, XL 320→448.
  The `TextureCache` 256 MiB budget comfortably holds 512 px thumbs (~1 MiB each).
- **Index v12:** `VaultSettings::migrated_thumb_side` (u16, 0 = legacy) appended
  last in the settings block, version-gated on read. `Vault::create` stamps it (and
  `migrated_index_version`) so fresh vaults are never offered a pointless upgrade —
  found live: an unstamped fresh vault triggered a bogus "sharpen thumbnails" offer.
- **MigrationJob thumb arm** (same coordinator + decode-pool shape): images with a
  stored thumb are re-decoded from originals and re-thumbnailed at 512 (the same
  item also sniffs the animated flag when applicable — one item per image, mirroring
  `scan_migration`'s arm parity); known-codec videos re-probe for a 512 poster;
  Unknown-codec videos that resolve during the pass also get their existing poster
  replaced (`apply_video_probe` only fills empty spans). New vault free friends
  `apply_image_thumb` / `apply_video_poster` append the fresh chunk under
  `write_mutex_` and repoint the span; superseded chunks are dead ciphertext for the
  existing auto-compact. Cancel keeps committed work, does not stamp.
- Transfers lower the destination's `migrated_thumb_side` to the source's when the
  source is behind, re-offering regeneration (mirrors the Phase 65 rule).

## Verification

- 1999 tests / 0 failed, plain and `--asan` (34 tests added).
- Live headless app run (Xvfb + xdotool, two seeded vaults): pull-copy end-to-end,
  multi-select + descendant drop, root-tag materialization visible in the tag editor,
  favorite badge preserved, conflict Rename produced `beta_2`, "Copied 1 of 1 from
  source", XL grid with tag chips, and no migration offer on freshly created vaults.
- SonarCloud: quality gate OK, zero open issues on the PR.
