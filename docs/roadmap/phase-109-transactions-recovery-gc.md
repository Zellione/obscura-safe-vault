# Multi-file transactions, recovery, and garbage collection (Phase 109)

**Status:** complete

## Delivered

- `v3_mutation.*` is the sole write coordinator: it owns the kernel writer lease and the
  master-key copy, publishes an authenticated immutable object before committing its row,
  replaces new-first, and unreferences before deleting. A failed post-commit unlink is reported
  as `CleanupPending`; reconciliation can safely finish it later.
- `v3_db.*` commits an object reference and the monotonically increasing `app_generation` in one
  `BEGIN IMMEDIATE` transaction. Create, replace and delete return typed outcomes; replacement and
  deletion return the superseded object ID only after commit.
- `v3_recovery.*` takes a consistent object-reference snapshot, enumerates canonical object names
  through retained directory descriptors, reports missing references and size mismatches as
  corruption, and identifies unreferenced objects as garbage rather than deleting during scan.
- GC applies an age grace period, requires the writer lease at its API boundary, rechecks the live
  database immediately before each unlink, verifies the candidate's current length, and durably
  unlinks. An object referenced after the scan survives.
- Staging recovery only recognizes the exact `tmp-` plus 32-lowercase-hex namespace created by
  this vault. It removes old, owner-only, single-link regular files under the writer lease;
  recent and malformed/foreign entries are preserved and reported.
- Fault tests cover write, file-sync, no-replace publish, directory-sync, database rejection,
  cleanup failure semantics, cold reopen, missing objects, and the scan/GC reference race.

Final verification: 2,327 Debug tests pass; Release, ASAN and no-AV results are recorded in the
phase PR checks.

## Goal

Turn the filesystem and database primitives into a crash-consistent storage engine despite the absence of a transaction spanning both.

## Step-by-step work

1. Implement one mutation coordinator as the only owner of DB write transactions and object publication/deletion. Background jobs may prepare encrypted staging objects but may not publish/reference them independently.
2. Encode the publish-before-reference protocol for create/import, new-first swap for replacement, and unreference-before-delete for deletion.
3. Store enough generation/state information in object rows to diagnose incomplete operations without treating an unauthenticated filename as authority.
4. On open, remove only well-formed old staging artifacts owned by this vault; preserve recent files while another valid writer lock exists; quarantine malformed entries rather than parsing paths loosely.
5. Build reconciliation: snapshot all referenced IDs in a consistent read transaction, enumerate strict object filenames root-relatively, report missing references as corruption, and mark unreferenced valid objects as garbage.
6. Implement garbage collection with a grace period and recheck under the writer lock immediately before unlink. Never delete an object that became referenced after the scan.
7. Replace `wasted_bytes`/hole punching/in-place compact semantics for v3 with orphan/staging/database free-page measurements, DB maintenance, and object GC. Keep legacy compact code isolated for legacy vaults.
8. Define cancellation: already committed batches remain valid; published but unreferenced objects are recoverable garbage; no half-mutated logical tree is exposed.
9. Add database transaction batching equivalent to the existing CommitLane while preserving generation order, error-stop behavior, flush-on-lock, and bounded loss expectations.
10. Build a subprocess crash harness that kills at every injected boundary and validates cold reopen plus reconciliation.

The executable fault matrix uses deterministic boundary injection followed by closing every
handle and cold reopening the SQLCipher database and vault root. The lower-level filesystem and
object-store suites already exercise every write/sync/rename/directory-sync boundary; Phase 109
adds the coordinator-level prior-or-new-state assertions without duplicating the encryption
fixtures in a second subprocess binary.

## TDD and fault tests

- Exhaustive create/replace/delete crash matrix before/after file sync, rename, directory sync, SQL begin/commit, unlink, and cleanup.
- Concurrent reader snapshots see either old or new references, never mixed metadata/object identity.
- GC race test where an object becomes referenced after scan; it must survive.
- Missing, duplicate, malformed, foreign-vault, hard-linked, and unauthentic object cases produce the specified repair/report outcome.
- Disk-full and cancellation tests remain retryable and never discard committed media.

## Acceptance criterion

Every injected crash reopens to a valid prior-or-new database state; no committed row references a partially published object; reconciliation reliably distinguishes corruption from reclaimable garbage; and v3 no longer needs monolithic compaction.
