# Complete lock boundary + secret-failure cleanup (Phase 94)

**Status:** 🔜 ready for review
**Date:** 2026-08-29

This is **Phase A** of the 2026-08-29 security audit remediation
(`AUDIT.md` / `AUDIT_IMPLEMENTATION.md`, phases A–G mapping to app phases
94–100). It fixes **OSV-AUD-001** (High — vault lock retains sensitive
settings) and **OSV-AUD-006** (Low — secret residue on keyfile and unlock
allocation failures). No `.osv` byte change.

## Findings being fixed

**OSV-AUD-001 — `Vault::lock()` did not establish its advertised security
boundary.** It stopped the commit lane, wiped the master key, reset the index
root, and cleared saved searches — but never reset `settings_`, whose
`SecureString` fields (tag category names, tag descriptions, template field
names, tag field values) are decrypted index-derived plaintext. Worse,
`vault_settings(const Vault&)` returned `settings_` without checking
`unlocked_`, so a caller holding a locked `Vault` could still read the
retained metadata. An opened-but-locked vault can live for an arbitrary
period.

**OSV-AUD-006 — secret residue on failure paths.** `read_keyfile()` read into
a plain `std::vector`: a partial read reported as failed released the bytes
already read without an explicit wipe. `UnlockJob::launch()` copied the
password first and the keyfile second; if the keyfile copy failed it returned
`false` without wiping the already-copied `pw_` — the password outlived the
failed operation unnecessarily.

## What shipped

### OSV-AUD-001 — lock is a complete decrypted-state boundary

- **`Vault::lock()` now clears `settings_`** (`src/vault/vault.cpp`) — after
  the CommitLane is stopped, the key wiped, the root reset, and saved searches
  cleared, `settings_ = VaultSettings{}` releases every settings `SecureString`
  to a wiping destruction. A re-unlock re-populates `settings_` from the index
  slot, so unlock/lock/unlock round-trips are unaffected.
- **`vault_settings(const Vault&)` now returns an immutable process-wide empty
  object while `!unlocked_`** (locked, or opened-but-not-yet-unlocked) instead
  of the retained `settings_` reference — defense in depth over the lock-time
  clear (the getter never reports a stale copy even if a future path ever
  wrote settings while locked, which `set_vault_settings` already refuses).
  The reference is read-only; callers may retain it across unlock harmlessly.
  All existing call sites read settings only while a vault is open/unlocked.
- The wipe-observation seam now covers `SecureString` and `SecureBytes`
  deallocation (`record_wipe_for_tests` in `free_storage`), so the regression
  test can *prove* the settings allocations are released and zeroed at lock.

### OSV-AUD-006 — secret failure paths wipe

- **`read_keyfile()` returns `crypto::SecureBytes`** (`src/platform/paths.*`) —
  mlock'd best-effort and wipe-on-release, so every failure path (partial read,
  too-large file, allocation failure) wipes the bytes already read. The size-
  then-one-read strategy is preserved (no chunk-growing vector to strew key
  material). Both callers (`UnlockScreen::submit`, `VaultUnlockPicker::try_unlock`)
  now hold the keyfile in a `SecureBytes` and pass `.as_span()` — their
  hand-written `crypto_wipe` calls become redundant and are removed.
- **`UnlockJob::launch()` wipes both secrets on every failed launch** — a
  partial copy (keyfile `SecureBytes` allocation failure, via the shared
  `inject_secure_allocation_failure` seam — which `SecureBytes::resize` now also
  honours, matching `SecureString`) and a worker-thread creation failure
  (the `std::jthread` construction is wrapped; anything thrown — `std::system_error`
  or an allocation failure — rolls `pw_`/`keyfile_` back to empty and resets
  `active_`/`done_` so `take_outcome()` cannot hang).

## Tests

- `tests/crypto/test_secure_string.cpp` — `secure_string_destruction_records_wipe_observation`.
- `tests/crypto/test_secure_bytes.cpp` — `secure_bytes_destruction_records_wipe_observation`,
  `secure_bytes_injected_allocation_failure_returns_false`.
- `tests/vault/test_vault_settings.cpp` — `vault_lock_clears_settings_metadata`
  (category + description + template field + field value all gone after lock),
  `vault_settings_opened_not_unlocked_returns_empty`,
  `vault_lock_wipe_observation_clears_settings_allocations` (a fresh vault's
  only lock-time wipe source is the seeded settings, so the observation-count
  increase specifically proves them released and zeroed).
- `tests/ui/test_unlock_job.cpp` — `unlock_job_keyfile_copy_failure_wipes_password`,
  `unlock_job_thread_launch_failure_wipes_secrets` (via the new
  `ui::test_only_force_unlock_thread_failure` hook).
- `tests/platform/test_paths.cpp` — `paths_read_keyfile_returns_secure_bytes`
  (size + mlock asserted), `paths_read_keyfile_partial_read_wipes_bytes_already_read`
  (via the new `platform::inject_keyfile_short_read` seam), plus the existing
  keyfile round-trip tests updated to the `SecureBytes` return type.

2207 tests / 0 failed (baseline 2197 + 10); ASAN clean; TSan clean (no new
races); Release + no-FFmpeg (2029/0) parity legs green.

## Deliberately unchanged

- **No `.osv` byte change, `INDEX_VERSION` stays 12.**
- **`read_file()`** (metadata JSON imports, non-secret) still returns a plain
  `std::vector<uint8_t>` — only keyfile material needs the wiping type.
- **`crypto::SecureBuffer`** (fixed-size key buffers) does not join the wipe
  observation seam — it has no heap allocation to observe; its wipe guarantee
  is unchanged.
- **The worker still wipes `pw_`/`keyfile_` after a successful vault call**
  — the Phase 94 work adds the failure paths, it does not touch the success
  path.

## Memory graph updates

- `mem:module/vault` — lock() now clears `settings_`; `vault_settings()` returns
  the immutable empty object while locked.
- `mem:module/ui` screens — `UnlockJob` wipes both secrets on every failed
  launch (partial copy or thread-creation failure).
- `mem:module/app` — `read_keyfile()` returns a wiping `crypto::SecureBytes`.