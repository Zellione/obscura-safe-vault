# Secure directory and object-store filesystem primitives (Phase 106)

**Status:** complete

## Goal

Build the Linux filesystem boundary required by a directory vault without yet storing user content. Every operation is relative to an already-open vault-root directory descriptor and fails closed on symlinks, replacement races, escape attempts, unexpected file types, or ownership/permission violations.

## Step-by-step work

1. Add RAII types for a vault-root handle, child directory handle, advisory lock, and durable output file; make them move-only and close-on-exec.
2. Open a chosen root with `openat2` resolution constraints (`BENEATH`, `NO_SYMLINKS`, `NO_MAGICLINKS`) and a conservative `openat` component-walk fallback. Never re-resolve a validated absolute path for later I/O.
3. Create new roots with mode `0700`, internal directories with `0700`, and control/object files with `0600`, independent of an overly permissive umask.
4. Validate the exact root shape and reject symlink roots, nested symlinks, devices, sockets, FIFOs, hard-linked control files, unexpected ownership, and duplicate/unknown required control files. Define how future optional files are admitted by a feature flag.
5. Implement exclusive, no-replace staging creation with CSPRNG names; durable file flush; atomic same-filesystem publication; directory `fsync`; safe unlink; and empty-directory cleanup.
6. Implement the single-writer lock and read-only/open semantics. Include PID only as a diagnostic; kernel lock ownership is authoritative. Ensure shutdown joins jobs and closes DB/object readers before releasing/wiping keys and the lock.
7. Add checked path construction for the fixed `objects/<two-hex>/<object-id>.osvo` grammar. User names never enter filesystem paths.
8. Add deterministic fault-injection hooks for open, write, file sync, rename, directory sync, lock, and unlink, with a cold-reopen harness after each injected failure.
9. Add root creation rollback that removes only artifacts created by the current failed creation attempt and never recursively removes a caller-provided non-empty directory.
10. Document supported filesystems and the requirement for same-filesystem atomic rename; reject a staging directory redirected to another mount.

## TDD and security tests

- Symlink-swap and rename-race tests at every path component.
- Parallel creators cannot claim the same root, object ID, or lock.
- Permissions remain owner-only under multiple umasks.
- Disk-full/short-write/interrupted-sync sweeps leave either the prior state or an identifiable unpublished file.
- Malformed names, shard mismatch, hard links, unexpected types, and traversal attempts fail without touching outside paths.
- Lock is released on all error paths and never before live workers have stopped.

## Acceptance criterion

Filesystem primitives pass unit, race, and fault-injection tests; no operation follows a link or escapes the opened root; creation is durable or cleanly retryable; and `scripts/test.sh`, Release, no-AV, and ASAN gates are green.

## Delivered

- `src/vault/v3_fs.*` adds move-only vault-root, writer-lock, and durable staging-file
  handles. Root opening prefers `openat2` with `RESOLVE_NO_SYMLINKS |
  RESOLVE_NO_MAGICLINKS`; its fallback walks every component through retained
  directory descriptors with `O_NOFOLLOW`.
- New layouts are exclusively claimed and fixed to `0700`/`0600` independently
  of umask. Failed creation removes only artifacts from that new attempt; an
  existing caller directory is never recursively removed.
- Layout validation rejects wrong ownership/modes, links, hard-linked control or
  object files, special files, unknown root entries, malformed shards/object
  names, and `objects`/`staging` mounts on a different device.
- Staging uses CSPRNG names and exclusive creation. Publication requires a
  successful `fdatasync`, uses `renameat2(RENAME_NOREPLACE)`, then syncs the
  affected directories. Object paths are derived only from 128-bit IDs using
  the fixed lowercase shard grammar.
- The `flock`-backed writer lease is authoritative and move-only; the PID written
  to `lock` is diagnostic only. Read-only root handles never acquire it.
- Deterministic one-shot hooks cover open, write, file sync, rename, directory
  sync, lock, and unlink failure boundaries. Tests exercise cold validation,
  rollback, permissions under a zero umask, symlink/hard-link rejection,
  canonical path parsing, lock contention across processes, publication, and
  cleanup.

### Filesystem contract

Directory vaults require a local Linux filesystem that provides durable regular
file and directory `fsync`, advisory `flock`, and atomic same-filesystem
`renameat2(RENAME_NOREPLACE)` semantics (the intended deployment set is ext4,
XFS, and btrfs). Network filesystems and filesystems that do not honor those
durability/locking semantics are unsupported. `objects/` and `staging/` must be
on the vault root's device; cross-device publication fails closed (`EXDEV`) and
layout validation rejects a redirected mount before use.

Verification: 2,303 Debug tests pass; the pre-coverage-fix Release and no-AV
suites passed 2,301 and 2,107 tests respectively,
and the final filesystem suite is clean under ASAN/UBSAN. The managed runner's
ptrace wrapper prevents LeakSanitizer startup; the full ASAN/UBSAN suite passes
with leak detection disabled and reports no address or undefined-behavior issue.
