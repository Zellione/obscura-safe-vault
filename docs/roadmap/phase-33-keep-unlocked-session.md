## Phase 33 — Option to keep a vault unlocked for the session ✅

**Goal:** Let the user opt out of the Phase 14 PR5 5-minute idle auto-lock for
the currently unlocked vault, entirely **in-memory and session-scoped** — the
choice is never persisted to disk and never survives a lock, vault switch, or
app restart, so it cannot weaken the vault's at-rest security.

### Tasks
- [x] **Session-only toggle** — `U` in `GalleryGrid` requests
  `NavKind::ToggleKeepUnlocked`; `App::apply_nav` flips an in-memory
  `keep_unlocked_` bool in place (no screen swap). No new field on
  `VaultRegistry` or any on-disk preference file.
- [x] **Wire into the idle timer** — `src/app/auto_lock.h`'s
  `should_auto_lock(has_active, blocks_idle_lock, keep_unlocked, timer, dt)`
  is a pure extraction of `App::maybe_auto_lock`'s three suppression guards
  (no active vault / a screen's `blocks_idle_lock` / the new toggle), all of
  which reset the existing `IdleTimer` instead of ticking it. The manual
  `Lock` action, vault-switch, and app exit are unaffected — they always
  wipe the master key as before.
- [x] **Always resets** — cleared in `App::promote_pending()` (vault switch)
  and the `LockActive` nav case; re-unlocking (even the same vault) always
  starts with auto-lock **on**. It cannot be pre-set before unlock.
- [x] **Visible indicator** — `App::render_frame` draws a small corner badge
  ("Auto-lock off [U]", `draw_keep_unlocked_badge`) over whatever screen is
  active whenever `active_ && keep_unlocked_`. An App-level overlay (not a
  per-screen one), so it stays visible across every navigation without
  threading the flag through each screen's constructor.
- [x] Updated `CLAUDE.md` (module layout) + `mem:core`.
- [x] `tests/app/test_auto_lock.cpp` — 5 pure unit tests for
  `should_auto_lock` (mirrors the existing `IdleTimer` tests): each
  suppression guard (no active vault / blocks_idle_lock / keep_unlocked)
  returns false and resets the timer even with a huge `dt`; normal firing
  behaviour is unchanged when nothing suppresses; disabling `keep_unlocked`
  after a long suppressed period starts counting fresh instead of firing
  immediately.

**Out of scope (YAGNI):** persisting the preference (per-vault or globally)
across restarts; changing the 5-minute default; disabling the manual `Lock`
action; keeping more than one vault unlocked at once (still single-active,
Phase 14).

### Acceptance criterion
With the toggle on, an unlocked vault survives more than 5 minutes of
inactivity without locking; with it off (the default on every unlock), the
existing auto-lock timing is unchanged; explicit lock, vault switch, and app
restart always require the password again regardless of the toggle.

**Status:** ✅ 722/722 tests pass (`scripts/test.sh`); `scripts/test.sh --asan`
clean. Full interactive verification (launching the app, toggling `U`, and
visually confirming the badge) was not possible in this sandboxed headless
environment — SDL exits immediately under both the `dummy` driver and Xvfb
with no window manager, independent of this change. The app and test binary
both build clean; the decision logic is fully unit-tested and the App-level
wiring was reviewed by hand.
