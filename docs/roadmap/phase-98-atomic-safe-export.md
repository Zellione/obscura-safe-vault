# Atomic, link-safe export creation (Phase 98)

**Status:** ✅ shipped
**Date:** 2026-08-31

This is **Phase E** of the 2026-08-29 security audit remediation
(`AUDIT.md` / `AUDIT_IMPLEMENTATION.md`, phases A–G mapping to app phases
94–100). It fixes **OSV-AUD-005** (Medium — export path validation is vulnerable
to a filesystem race, CWE-367/CWE-59). No `.osv` byte change.

## Finding being fixed

**OSV-AUD-005 — the export sink was check-then-truncating-open.** `export_images`
resolved a collision-free name (probing with `fs::exists`), containment-checked
it with `weakly_canonical`, and *later* opened it with `fopen(path, "wb")`. The
check and the use were two separate filesystem operations: a local attacker could
create the candidate or replace a path component with a symlink/reparse point
between them, and the truncating `"wb"` open would follow the link and overwrite
a target outside the chosen folder.

## What shipped

### `platform/atomic_file.{h,cpp}` — the one sanctioned export sink

New SDL-free module (a documented platform/ layering exception, like
`path_utf8.h`). `create_new_file_within(directory, safe_component)` returns an
**already-open, exclusively-created** `NewOutputFile` (move-only RAII that closes
the handle; `display_path` for logs/status only — never reopened):

- **Evaluation is atomic.** The exclusive create *is* the collision test
  (O_EXCL / CREATE_NEW), so there is no probe-then-open window in which another
  process's file can be truncated. Collision naming (`"name (n).ext"`, the Phase
  10 format) rides the create loop, bounded at 10,000 attempts.
- **No symlink following.** Linux `openat2` with `RESOLVE_BENEATH |
  RESOLVE_NO_SYMLINKS` (kernel 5.6+; `openat(...|O_NOFOLLOW|O_EXCL)` fallback when
  the syscall or a seccomp policy is unavailable; `ELOOP` from the no-symlinks
  resolve is treated as "name taken" and suffixed past). Windows `CREATE_NEW`
  then `GetFinalPathNameByHandleW` on the final handle, containment-compared
  against the directory's own final (junction/mount-resolved) path — a reparse
  point at the candidate fails `CREATE_NEW`, and an intermediate junction
  redirecting the create outside the folder is detected and the file discarded.
- **Containment enforced at the boundary.** On Linux creation happens relative
  to a long-held directory descriptor, and `safe_component` is validated to be
  one plain filename (no separator, not `.`/`..`, no NUL, ≤ 255 bytes) before any
  syscall — so an escaping name is refused before it becomes a path.
- **Permissions** stay `0666 & ~umask` on POSIX / default DACL on Windows:
  exports are intended to be *readable* user files, the opposite of the
  owner-only keyfile/vault rule — deliberately not the `create_owner_only_file`
  treatment.
- **Test seam** `inject_atomic_create_collision()` deterministically simulates an
  attacker claiming a candidate between attempts (the exact race being fixed);
  disarmed after one use.

### `ui::export_*` re-wired onto the handle

- `export_one_media(vault, node, NewOutputFile out, scratch)` now writes to `out.fp`,
  flushes, closes, and wipes `scratch` on every path. It **never reopens a path**.
- `export_images` replaces the `fs::exists` probe + `unique_export_path` +
  `fopen("wb")` with `sanitize_node_name` → `create_new_file_within`. The
  decrypted bytes live in mlock'd `SecureBytes` straight through to `fwrite`.
- `unique_export_path` is **deleted** — its job (collision naming) moved into the
  atomic helper where it is race-free; the pure `exists`-probe template cannot
  exist in a race-free sink. `export_path_within` is retained only as a cheap
  post-create invariant assertion on `display_path`.
- Consent, selection-only output, cancellation-between-files, and the
  "existing bytes never truncated" behavior are all unchanged.

## Tests

- `tests/platform/test_atomic_file.cpp` (12): basic create + write-through-handle
  + RAII close; collision suffixing preserves the existing file verbatim; multi-
  collision skip; no-extension suffix; unsafe-component rejection (including a
  `256`-byte name) creates nothing; symlink at the candidate never followed and
  its outside target never touched (POSIX); dangling-symlink candidate is
  refused without unlinking (POSIX); a *file* as the destination directory is
  refused (POSIX); the injected attacker-race seam produces a suffix without
  leaving the plain name behind; 8 concurrent creators all succeed with distinct
  names — exactly one winning `same.png`; missing directory → `nullopt`.
- `tests/ui/test_export.cpp`: `export_one_media_writes_verbatim_and_wipes_buffer`
  re-based on `create_new_file_within`; new `export_one_media_write_failure_wipes_scratch`
  (a read-only handle forces `IoError` and proves the scratch wipe on the failure
  path); new `export_symlink_candidate_is_not_followed` (POSIX). The three
  `unique_export_path` pure tests are removed with the function. Existing
  batch/collision/video/traversal/consent tests unchanged and green.
- Windows reparse-point coverage: `CreateFileW(CREATE_NEW)` + the final-handle
  containment check unit, exercised through the same collision seam (CI lacks
  SeCreateSymbolicLinkPrivilege to build real reparse points).

## Verification

- `scripts/test.sh`: **2248 tests / 0 failed** (baseline 2237 + 11).
- `scripts/test.sh --asan`: 2248/0 clean.
- `scripts/test.sh --tsan`: 2248/0 clean, no new races.
- `git diff --check` clean.
- Windows CI + SonarCloud results recorded on the Phase 98 PR.

## Deliberately unchanged

- **No `.osv` byte change, `INDEX_VERSION` stays 12.**
- **`export_path_within`** still exists (pure, tested) — its role changed from
  pre-write gate to post-create assertion; removing it would delete dead weight
  but also remove a useful invariant check, so it stays.
- **Export permissions** remain the default readable-user-file mode — exports
  are the user's content, not secrets; owner-only treatment is for keyfiles and
  vaults.
- **Whole-vault rollback** (replaying an old exported file over a newer one) and
  the export-consumer's own downstream handling are outside this finding's scope.

## Memory graph updates

- `mem:module/app` — new `platform/atomic_file.*`: `create_new_file_within` is the
  sole atomic no-follow export sink; collision suffixing now lives there.
- `mem:module/ui` — export writes only to the atomic handle; `unique_export_path`
  deleted; `export_path_within` demoted to post-create assertion.
- `mem:core` — invariant 6 sink wording updated (atomic create instead of
  containment-check on export).
- AGENTS.md — invariant 6 reference refreshed; invariant-1 export exception note
  now lists atomic no-follow creation among its mitigations.