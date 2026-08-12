# Phase 82 — `Shift+C` refused to compact a vault with real waste

## Owner report

> "please verify if Shift + C is doing something. Because I deleted 1000+ files
> and it still says no significant space to reclaim."

## Symptom

Pressing `Shift+C` on the gallery grid printed `No significant waste to reclaim.`
in the footer and never opened the compact-confirm modal — after a bulk delete
of over a thousand files. No figure was shown, so there was no way to tell
whether the vault genuinely had nothing to reclaim or the app was declining to
act, and no override.

## Root cause

`handle_shift_c_key` (`src/ui/gallery_grid.cpp`) gated the keypress behind
`ui::should_display_waste`:

```cpp
threshold = max(50 MiB, file_size / 10)      // src/ui/waste_threshold.h
```

That predicate exists for the **passive footer hint** added in Phase 26 — a
"significance" heuristic tuned so a busy grid is not nagged about a few KiB of
slack. Reusing it as the *permission gate* for an explicit user action inverted
its purpose: the larger the vault, the more waste it takes to be allowed to
reclaim any.

Two gates then leave a dead band with no way out:

| gate | formula | on the reported 699 MiB vault |
|---|---|---|
| `Vault::auto_reclaim_space` | `waste >= 256 KiB` **and** `waste * 4 >= size` | needs **175 MiB** |
| `ui::should_display_waste` (as used by `Shift+C`) | `waste >= max(50 MiB, size/10)` | needs **69.9 MiB** |

A thousand small files (≈50 KB each ≈ 50 MB of orphaned chunks) clears neither.
Nothing is reclaimed automatically, and the manual escape hatch refuses too.

Measured on the reported vault before the fix:

```
logical size      732,938,372 B   (699 MiB)
allocated blocks  1,429,400 × 512 = 731,852,800 B   (698 MiB)
filefrag          105 extents, ~1 MiB of punched holes total
```

Allocated ≈ logical confirms `auto_reclaim_space` had long since stopped firing.
Note also that `vault_reclaim()` only punches holes — it never truncates — so on
Linux the *logical* size only ever grows. `compact()` is the sole path that
shrinks the file, and it was exactly the path behind the gate.

The compact machinery itself was never at fault: `Shift+C` → `confirm_compact`
→ `FileOpJob::start_compact` → `run_compact` → `Vault::compact` was correct and
covered by tests throughout. Only the gate in front of it was wrong.

## Fix

New pure predicate `ui::should_offer_compact(wasted_bytes)` — true for any
non-zero waste. `handle_shift_c_key` uses it instead of `should_display_waste`.

This is safe because the modal it opens is already the right place for the
judgement call: it states the exact amount ("Reclaim 47.3 MB of wasted space.")
and defaults to cancel. The user decides whether the I/O is worth it; the app
no longer decides for them.

The refusal message now only fires when there is genuinely nothing at all, and
says so plainly: `Nothing to reclaim — the vault has no wasted space.`

`should_display_waste` is unchanged and still governs the footer hint, which is
what it was written for.

## Not changed (deliberately)

- **`auto_reclaim_space`'s ratio gate.** Loosening it would put a full
  hole-punch scan (or, off Linux, a whole-file compact) on every delete. The
  I/O trade-off it encodes is still right; `Shift+C` is now the escape hatch it
  was always meant to be.
- **`vault_reclaim()` still does not truncate the dead tail.** Making ordinary
  deletes shrink the `.osv` is a real improvement but touches the
  crash-safety-critical reclaim path, so it belongs in its own phase.

## Tests

`tests/ui/test_waste_threshold.cpp`
- `should_offer_compact_any_nonzero_waste`
- `should_offer_compact_below_display_threshold` — pins the two predicates
  apart at the reported vault's exact numbers (699 MiB / 50 MiB waste).

`tests/ui/test_file_op_job.cpp`
- `file_op_job_compacts_waste_the_footer_hint_hides` — end-to-end: seed a
  4 MiB + 512 KiB vault with incompressible payloads, delete the smaller image,
  assert the orphan is real, assert `should_display_waste` hides it while
  `should_offer_compact` offers it, then run the real `FileOpJob` compact and
  assert the file shrank and the surviving image is intact.

Sizing follows the `test_migration_job.cpp` precedent: keep ≫ delete so waste
stays under `auto_reclaim_space`'s ratio gate. That gate hole-punches on Linux
but runs a truncating `compact()` everywhere else, so letting it fire would
leave nothing for the test to compact on Windows.

## Format impact

None. No `.osv` change; `INDEX_VERSION` stays 12.
