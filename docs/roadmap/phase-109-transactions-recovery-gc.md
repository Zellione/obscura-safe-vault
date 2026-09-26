# Multi-file transactions, recovery, and garbage collection (Phase 109)

**Status:** not started

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

## TDD and fault tests

- Exhaustive create/replace/delete crash matrix before/after file sync, rename, directory sync, SQL begin/commit, unlink, and cleanup.
- Concurrent reader snapshots see either old or new references, never mixed metadata/object identity.
- GC race test where an object becomes referenced after scan; it must survive.
- Missing, duplicate, malformed, foreign-vault, hard-linked, and unauthentic object cases produce the specified repair/report outcome.
- Disk-full and cancellation tests remain retryable and never discard committed media.

## Acceptance criterion

Every injected crash reopens to a valid prior-or-new database state; no committed row references a partially published object; reconciliation reliably distinguishes corruption from reclaimable garbage; and v3 no longer needs monolithic compaction.
