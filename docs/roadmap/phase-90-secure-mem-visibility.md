# Secure-memory state invisible + no system-config guidance (Phase 90)

**Status:** 🔜 ready for review
**Date:** 2026-08-26

## Problem

Two gaps the break-in/hardening effort (`docs/break-in-effort.md`) surfaced:

- **(a) The secure-memory state was invisible to a GUI user.** The app warns
  at startup when the page-lock budget is below 256 MiB and again (once) when
  an mlock fails — but both go to `stderr`, which a windowed app's user never
  sees. So "your decoded images may be swappable" was knowable only by
  reading the code.
- **(b) No system-config guidance.** The README's `RLIMIT_MEMLOCK` section
  didn't mention the app's own 256 MiB startup grow, and didn't explain that
  `mlock` keeps data out of *swap* but not out of RAM (zram swap, true
  hibernate, Windows `hiberfil.sys`). The Debugging section didn't state that
  Linux **Debug** builds keep core dumps by design — the *exact* vector of the
  Phase 1 break-in — or that a Debug crash's core is as sensitive as the vault.

## What shipped

- **Live status line in the F1 help popup** (Global group, src/ui/help_popup.*):
  `Secure memory: 8 MiB page-lock budget active (best-effort)` or, once any
  buffer degrades, `… — some decoded data is swappable (mlock exhausted)`.
  - `platform::lockable_budget_bytes()` (src/platform/harden.*) — the budget
    the process actually has: soft `RLIMIT_MEMLOCK` on Linux (`SIZE_MAX` when
    unlimited), minimum working-set on Windows; `0` if unreportable.
  - `crypto::mlock_failure_seen()` (src/crypto/secure_mem.h) — the process-wide
    "an mlock failed" flag, made a monotonic, queryable `inline
    std::atomic_bool`; `should_warn_mlock_once()` semantics unchanged.
  - `ui::secure_mem_status_line(budget, degraded)` — pure in its inputs so the
    wording is unit-tested without a platform; an empty-key `HelpEntry` renders
    without the `[]` brackets.
- **README updates:** the `RLIMIT_MEMLOCK` section now documents the 256 MiB
  startup grow, the F1 status line, and the swap-vs-RAM / zram / hibernate /
  `hiberfil.sys` caveat; the Debugging section gains a "Linux core dumps (Debug
  builds)" note — Release disables dumps (`platform::disable_core_dumps()`),
  Debug keeps them for debuggers, so treat a Debug crash's core as
  vault-sensitive (`coredumpctl list` / `sudo rm …`) and prefer Release for a
  live vault.

## Tests

- `secure_mem_status_line_reports_active_budget`, `..._reports_degraded_budget`,
  `..._rounds_up_sub_mib_budgets`, `..._reports_unlimited_budget`
  (tests/ui/test_help_popup.cpp) — the wording, including the sub-MiB rounding
  (a 1 KiB budget reads "1 MiB", not "0 MiB") and the unlimited case.
- `lockable_budget_bytes_is_positive` (tests/platform/test_harden.cpp).
- `mlock_failure_seen_tracks_the_warn_gate` (tests/crypto/test_secure_bytes.cpp)
  — order-independent: after the first warn-gate call `seen()` is true and
  stays true, while the gate itself stays exhausted.

Both wording paths were also verified in the running app under Xvfb: the
default-budget line, and the degraded line with `ulimit -l 0`.

2144 tests / 0 failed (baseline 2138 + 6); ASAN clean.

## Deliberately unchanged

- The 256 MiB budget, the warn-once + degrade-to-swappable contract, and the
  Release-only core-dump suppression — Phase 90 makes the *state visible* and
  documents the host-policy decisions; it changes none of the policies.
- The `.osv` format and `INDEX_VERSION`.
