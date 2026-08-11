# Copy-capable gallery combine (Phase 76)

**Status:** ✅ shipped
**Date:** 2026-08-11

## Problem

Move/copy with gallery merging is meant to be possible everywhere, in and across
vaults, mixed galleries included. An audit of every surface found the machinery
almost complete: the `M` transfer dialog (push and pull, single gallery,
multi-gallery, mixed grid selections, and the favorites/tag/search collection
screens) already pre-scans for same-named destination galleries and offers
**Combine / Rename / Cancel**, with the Combine policy honoring the chosen
Move/Copy mode — and the vault layer (`vault::combine_galleries`) has handled
mixed galleries (media + sub-galleries as siblings) since Phase 46 and Copy mode
since Phase 71.

Two gaps remained:

1. **The dedicated combine dialog (`Shift+M`) was Move-only.** It had no
   Move/Copy stage, and `FileOpJob::start_combine` called `combine_galleries`
   without a mode argument, so the Phase 71 Copy-combine capability was
   unreachable from the one dialog built specifically for combining.
2. **Stale `combine.h` docs** still claimed two galleries are "structurally
   incompatible (one holds media, the other holds sub-galleries)" — a
   pre-Phase 46 restriction that no longer exists in the code and would have
   misled future work.

## What shipped

- **`Shift+M` now asks Move or Copy first.** `CombineDialog` gains a `Mode`
  stage (same Up/Down/Enter convention as the transfer dialog's): **Move**
  merges and deletes the source gallery once it empties (the historical
  behavior, still the default); **Copy** unions the source into the destination
  and leaves the source — media, sub-galleries, and shell — untouched. The
  stage precedes destination-vault/gallery picking, so same- and cross-vault
  combines both get it.
- **Mode is threaded through the job layer.** `FileOpJob::start_combine` takes
  a `vault::TransferMode` and forwards it to `combine_galleries`; the outcome
  wording follows the verb ("N copied" vs. "N moved"), and a Copy of an empty
  source no longer mis-reports `source_gone` (post-combine navigation stays
  put — the source still exists).
- **Shared option-row widget.** The Move/Copy (and Direction) row rendering is
  now one `ui::draw_option_rows` helper in `ui/widgets.*`, used by both
  TransferDialog and CombineDialog so the two dialogs cannot drift apart
  visually.
- **Docs corrected.** `combine.h` no longer claims structural incompatibility;
  mixed-gallery combine is spelled out (media children first, then sub-gallery
  children) and `combine_target_galleries` is documented as offering every
  gallery except (same-vault) the source and its descendants.

## Tests

- `combine_copy_mode_mixed_source_copies_both_kinds` (vault) — cross-vault
  Copy-combine of a mixed source: destination gains the media file and the
  whole subtree; source keeps everything.
- `file_op_job_combine_copy_keeps_source` / `file_op_job_combine_move_empties_source`
  (ui) — first coverage of `start_combine` through the worker job: Copy keeps
  the source intact and says "copied"; Move empties the source and says
  "moved".

2002 tests / 0 failed; ASAN clean.

## Deliberately unchanged

- The transfer dialog's Conflict stage (Phase 71) — it already offered
  mode-aware Combine everywhere, including pull transfers (Phase 75).
- `combine_galleries` semantics — collision files still skip (never overwrite),
  partially-merged sources survive for a later retry, tags still union
  case-insensitively.
- Combining into a *media*-named child still refuses (`AlreadyExists`) — a
  gallery cannot merge into an image.
