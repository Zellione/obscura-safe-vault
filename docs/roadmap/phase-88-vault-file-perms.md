# Vault files are not owner-only — 0644 on creation, silently truncated (Phase 88)

**Status:** 🔜 ready for review
**Date:** 2026-08-26

## Problem

Phase 4 of the break-in/hardening effort (`docs/break-in-effort.md`) established
that the threat model is **local**: an attacker who can read the user's home
tree can read everything that lands there with loose permissions. A `.osv`
vault is the same class of secret as a keyfile — the whole point of the
container is that the *file* may be stolen and only the password/keyfile keeps
it closed (Phase 5 quantified that the KDF makes offline cracking infeasible
at reasonable password lengths).

But the vault file itself was not treated that way:

- **New vaults** were created with `fopen(path, "w+b")`
  (src/vault/vault.cpp) → permissions `0666 & ~umask`, i.e. **0644,
  group/other-readable** under a default umask — while the keyfile that
  protects it gets explicit 0600 / current-user-only DACL
  (`platform::open_new_keyfile`, paths.cpp). The more secret file had looser
  permissions than the less secret one.
- **Worse:** `"w+b"` **silently truncates an existing file**. Creating a
  vault over an existing one destroyed it with no error — a data-loss path
  the exclusive-create keyfile already guards against ("clobbering a keyfile
  would permanently lock every vault bound to the old one").
- **Existing vaults** kept whatever permissions they had — a vault copied
  from a shared folder, or created under a loose umask, stayed group/other
  readable for its whole life.

## What shipped

- **`platform::create_owner_only_file(path, FILE*&)`** →
  `OwnerOnlyCreate{Ok, AlreadyExists, Error}` (src/platform/paths.{h,cpp}):
  atomically claim a brand-new file that only the current user can read —
  `O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC` + `0600` (umask-independent) on POSIX,
  `CREATE_NEW` + an explicit **current-user-only DACL** on Windows. The DACL
  construction was factored out of `open_new_keyfile` into a shared
  `build_owner_only_acl` (keyfiles and vaults are the same secret class;
  one implementation of the guarantee).
- **`platform::ensure_owner_only_file(path)`** — best-effort tightening of an
  EXISTING vault at open time: `chmod 0600` on POSIX, `SetNamedSecurityInfoW`
  with the current-user DACL on Windows. Never fails the open — a vault on a
  share owned by someone else may not be tighten-able — and logs ONE generic
  diagnostic on failure (no path, no secrets, per invariant #5).
- **`Vault::create`** uses the exclusive owner-only create; an existing file
  is now refused with the new `VaultResult::AlreadyExists` result instead of
  being truncated (the unlock screen maps it to
  "A vault already exists at that path.").
- **`Vault::open`** tightens the file to owner-only right after a successful
  open, so pre-existing vaults self-heal on the next unlock.

## Tests

- `vault_create_refuses_existing_file` — pre-existing file →
  `AlreadyExists`, content byte-for-byte preserved (both platforms).
- `vault_create_file_is_owner_only` — a new vault is exactly `0600`
  (POSIX; Windows covered by the shared DACL path + CI).
- `vault_open_enforces_owner_only` — a vault left at `0644` is tightened to
  `0600` by `Vault::open` (POSIX).

2134 tests / 0 failed (baseline 2131 + 3); ASAN clean.

## Deliberately unchanged

- The `.osv` format and `INDEX_VERSION` — a permissions change is invisible
  to the container.
- `open_new_keyfile`'s behaviour — only its DACL construction moved into the
  shared helper; semantics (write-only create, delete-on-failure) identical.
- Best-effort-ness of the existing-file tightening: a permission failure must
  not lock a user out of their vault; the warn-once log is the signal.
- Non-POSIX filesystems (FAT/exFAT) have no real permission bits; `chmod`
  there is a no-op by OS design — nothing to harden in that case.

## Windows sharing-mode lesson (found during CI bring-up)

`create_owner_only_file`'s first version requested `DELETE` access on the
long-lived vault write handle (`fp_`). Windows share compatibility is checked
in **both** directions: a later handle must grant sharing for every access bit
the earlier handle already holds. Since `fp_` stays open past `Vault::lock()`
(only `reset()` closes it), the next `Vault::open` / raw `fopen` — which grant
only `FILE_SHARE_READ|WRITE`, never `FILE_SHARE_DELETE` — failed with
`ERROR_SHARING_VIOLATION`. Pre-Phase-88 the handle came from `fopen("w+b")`,
which requests no `DELETE` access, so reopens coexisted. The fix: the create
handle requests only `GENERIC_READ|GENERIC_WRITE` (the owner-only guarantee
lives in the DACL, not the desired access); the error-path cleanup deletes via
`DeleteFileW` instead of a `DELETE`-access delete-on-close handle. `open_new_keyfile`
keeps its `DELETE` access because that handle is never open when the file is
reopened.
