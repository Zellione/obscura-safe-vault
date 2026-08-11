# Idle auto-lock fired during vault upgrade (Phase 79)

**Status:** ✅ shipped
**Date:** 2026-08-11

## Problem

Owner report: upgrading a ~400 GB vault (182 120 items) ran fast, then sat on
"Finishing… 182120/182120" for over an hour with zero CPU and an idle SSD.

Root cause: **the 5-minute idle auto-lock was never suppressed while a
`MigrationJob` ran.** `should_auto_lock`'s suppression set (no active vault /
`Screen::blocks_idle_lock` / keep-unlocked / busy import queue) has no entry
for the App-owned migration job: the gallery grid's `blocks_idle_lock()` only
reports its *own* jobs (`vault_busy` — file-op/transfer/combine), and
`ImportQueue::busy()` is false during a migration (the job takes the queue's
*exclusivity*, not tasks). The upgrade's long tail — the single batched
`commit_index()` fsync and the whole-file `compact()` — easily exceeds five
minutes of no user input on a large vault, so `App::maybe_auto_lock` fired
mid-flight and:

1. **Tore the vault down under the running coordinator.**
   `vault_state_.active->lock()` wipes the master key and clears the index
   tree, then `vault_state_.active.reset()` destroys the `Vault` the worker
   threads still hold a reference to — a use-after-free window. In the
   reported run the job survived long enough to fail cleanly
   (`commit_migration` → `Locked`) and finish.
2. **Wedged the UI permanently.** The `take_outcome()` poll in `App::update`
   was gated on `if (vault_state_.active)` — now null — so the finished job's
   outcome was never collected, `job->active()` stayed true forever, and the
   render path kept drawing the progress modal with phase `Done`:
   "Finishing… N/N", swallowing every input. Zero CPU, idle disk, no way out
   but killing the process.

The Phase 77 owner report ("stuck at Preparing… N/N") had the identical
signature; the fsync storm it fixed was real, but the *permanently* stuck
modal at phase `Done` can only happen when the poll is dead — i.e. that report
was very likely this same bug, and Phase 77's relabel changed what the stuck
screen said, not whether it stuck.

A third entry point of the same teardown-under-live-job hazard: closing the
window mid-upgrade. `SDL_EVENT_QUIT` is handled before the modal can swallow
it, `running_ = false` exits the loop, and `App::shutdown()` locked and
destroyed the vault with the coordinator still running.

## What shipped

- **Suppression (root cause).** `app::should_auto_lock` gains a
  `migration_active` parameter with the same suppress-and-reset semantics as
  the existing entries; `App::maybe_auto_lock` feeds it
  `migration_ui_.job && migration_ui_.job->active()`. A running upgrade can no
  longer be auto-locked, and the timer restarts from zero once the outcome is
  collected.
- **Un-gated outcome poll (defense in depth).** The `take_outcome()` poll in
  `App::update` moved out of the `if (vault_state_.active)` block — none of it
  needs the vault. If the vault ever goes away under a live job again via some
  future path, the modal now resolves to the result screen instead of wedging.
- **Offer modal on auto-lock.** The upgrade *offer* (job not yet started) does
  NOT suppress the auto-lock — locking an unattended vault is the right
  default there — but the firing lock now closes the offer and releases the
  import-queue exclusivity it held, so the modal cannot linger over the
  manager referencing a locked vault. It is re-offered at the next unlock.
- **`MigrationJob::abort_and_join()` (shutdown hardening).** Cancels, joins
  the coordinator, and deactivates without a `take_outcome()` poll (outcome
  discarded). `App::shutdown()` calls it before any vault teardown, and the
  destructor uses it too (a bare `jthread` join would block destruction until
  the full migration completed, uncancelled). Cancel semantics are unchanged:
  applied work is committed, the watermark stays unstamped, the upgrade is
  re-offered.

## Tests

- `auto_lock_false_and_resets_when_migration_active`,
  `auto_lock_migration_finish_starts_counting_fresh` (app) — a running job
  suppresses and resets the idle timer; a multi-hour upgrade leaves the timer
  counting from zero when it ends.
- `migration_job_abort_and_join_stops_job_without_polling` (ui) — mid-flight
  abort joins the coordinator, deactivates, returns no outcome, and leaves the
  vault unlocked and intact.
- `migration_job_abort_and_join_without_start_is_noop` (ui) — safe on a
  never-started job.

2013 tests / 0 failed; ASAN clean.

## Deliberately unchanged

- The auto-lock's security posture everywhere else: the upgrade **offer**
  modal, the result modal, and ordinary browsing still auto-lock after 5 idle
  minutes.
- `MigrationJob`'s cancel/commit contract and the `.osv` format.
- The wedge's UI-side symptom for *other* hypothetical teardown paths is
  additionally covered by the un-gated poll, but no such path is known to
  remain.
