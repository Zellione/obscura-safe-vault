## Phase 66 — Keep the destination vault open after cross-vault copy/move 🔜

**Goal:** Stop re-prompting for the destination vault's password on every
cross-vault Copy/Move. Today `TransferDialog`/`CombineDialog` (via
`VaultUnlockPicker`) unlock the destination transiently and wipe its key the
moment the dialog closes. This phase adds a single app-owned **warm slot** that
can keep that unlocked destination alive — for a sliding 5-minute window or for
the whole session — entirely **in-memory and session-scoped**. The first unlock
of any destination in a session always requires the password, so at-rest
security is unchanged.

### Modes

Chosen at the destination-unlock stage of the Transfer/Combine dialog
(a `←/→` selector row), pre-seeded from a persisted per-machine default:

1. **Lock immediately** — today's behavior; the shipped default.
2. **Keep open 5 minutes** — sliding window: every *completed* copy/move/combine
   into that vault resets it to 5:00. Expiry never fires while a transfer job
   into it is running.
3. **Keep open for the session** — never auto-locks; wiped only by an explicit
   lock, promotion/switch rules, or app exit. Exactly like Phase 33: choosing it
   is never persisted as *state* — the persisted F2 default only changes what
   the selector starts on and cannot unlock anything by itself.

### Tasks
- [ ] **`app::SecondVaultSession`** (new `src/app/second_vault.*`) — the one
  warm slot: destination path, unlocked `vault::Vault` handle, mode
  (`LockNow`/`KeepTimed`/`KeepSession`), sliding deadline. Decision logic is a
  pure, unit-testable model in the header (mirroring `app/auto_lock.h`):
  sliding reset on completed transfer, tick/expiry, defer-while-job-running,
  replace semantics (unlocking a *different* destination locks the previous
  warm vault first), explicit wipe.
- [ ] **Dialog integration** — `VaultUnlockPicker` gains the mode selector row
  at the Unlock stage and **skips the Unlock stage entirely** when the picked
  destination matches the warm slot. On completion with a Keep\* mode, the
  destination `Vault` handle *moves into the slot* instead of being wiped by
  `close()`; both `TransferDialog` and `CombineDialog` get this via the shared
  picker. Completed transfers into the warm vault call the slot's sliding
  reset.
- [ ] **Timer wiring** — `App::update` ticks the slot next to the existing
  `maybe_auto_lock`; expiry wipes through the normal `Vault` lock path
  (`crypto_wipe`, mlock'd key, `~Vault` backstop). Expiry is deferred while a
  transfer job that writes into the warm vault is active.
- [ ] **Password-free switch** — the vault manager and `` ` `` quick-switch
  show the warm vault with an "unlocked · m:ss" (or "unlocked · session")
  badge; opening it **promotes the warm handle to active with no password
  prompt** (the slot empties; the previously active vault locks exactly as
  today). The vault manager offers an explicit "lock now" action on the warm
  row.
- [ ] **Visible indicator** — an App-level corner badge (mirroring Phase 33's
  `draw_keep_unlocked_badge`) shows "2nd vault unlocked" plus the live
  countdown / "for session" whenever the slot is occupied. It does **not**
  fade while the second key is in memory.
- [ ] **Persisted default** — a per-machine preference stored beside
  `theme.conf` (never in the vault; contains no secret), surfaced as a new
  "Security — this machine" row in the `F2` settings overlay:
  Lock immediately / Keep 5 min / Keep for session.
- [ ] **Wipe paths** — any lock of the *active* vault (manual lock,
  `LockActive`, vault switch, quit — including the Phase 50 lock/quit confirm
  flow) also wipes the warm slot: locking up means locking everything. The
  one exception is switching to the warm vault itself, which promotes its
  handle to active rather than wiping it. No slot state survives an app
  restart.
- [ ] Update `mem:module/app`, `mem:module/ui`, `mem:ui_spec` (+ `mem:core`
  if the app module map changes) and `CLAUDE.md` hardening notes if needed.
- [ ] `tests/` — pure model tests for the slot (mode transitions, sliding
  reset on completed transfer, expiry, defer-while-job-running,
  replace-on-new-destination, explicit wipe); dialog-level skip-unlock;
  promote-to-active empties the slot; wipe on exit/manual lock; default mode
  `LockNow` preserves today's behavior byte-for-byte.

**Out of scope (YAGNI):** multiple simultaneous warm vaults (one slot only);
warming the *source* vault when switching away from it; a configurable
duration (5 minutes fixed); persisting Keep-for-session *state* across
restarts; any `.osv` format change (no `INDEX_VERSION` bump).

### Acceptance criterion
With **Keep 5 min**: after a cross-vault copy, a second copy to the same vault
within 5 minutes skips the password stage and resets the window; after expiry
the key is wiped and the password is required again. With **Keep for
session**: the destination never auto-locks; explicit lock or app exit wipes
it. A warm vault opens from the vault manager / quick-switch without a
password prompt. The corner badge is visible exactly while a second key is in
memory. With the default **Lock immediately**, behavior is identical to
Phase 65.

**Status:** 🔜 Planned. Design spec:
`docs/superpowers/specs/2026-08-06-phase66-keep-destination-vault-open-design.md`.
