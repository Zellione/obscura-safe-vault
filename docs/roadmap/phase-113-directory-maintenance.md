# Maintenance, backup, integrity, and operations (Phase 113)

**Status:** complete

## Progress

- ✅ Typed quick/deep verification with non-secret findings and full object-frame authentication.
- ✅ Race-safe garbage collection exposed through the directory-vault session/facade.
- ✅ Consistent encrypted snapshots: writer barrier, SQLCipher online backup, referenced immutable
  object copy, destination deep verification, and atomic no-replace directory publication.
- ✅ Restore to a new path with credential/integrity verification and source preservation.
- ✅ Operational cold-copy, snapshot, restore, finding, and maintenance guidance.
- ✅ Lossless thumbnail/poster rebuild and SQLCipher optimize/VACUUM maintenance actions.
- ✅ Live database/object/garbage metrics, staging cleanup metrics, and replacement of the
  directory-vault legacy compaction screen/help surface.
- ✅ Exclusive background progress UI, structured non-secret outcomes, and exact backup
  low-disk preflight requirements.
- ✅ 2,353 Debug tests and 2,353 ASAN/UBSAN tests pass.

## Goal

Provide safe operational tools for a vault made of many files: verification, recovery guidance, garbage collection, encrypted backup, and observable health.

## Step-by-step work

1. Add quick-open checks (header, DB key/schema, foreign keys, referenced-file presence) and an explicit deep verify that authenticates every database page/object frame and recomputes expected sizes.
2. Classify findings: fatal control/DB corruption; missing referenced object; unauthentic object; safe unreferenced garbage; stale staging; unknown future file; permission/link violation. Never silently convert corruption into deletion.
3. Add repair actions only where lossless: remove stale staging, GC rechecked unreferenced valid objects, rebuild derived thumbnails/posters from authentic originals, and repair database free space/indexes. Quarantine rather than overwrite suspicious files.
4. Implement encrypted backup/snapshot: acquire a consistent writer barrier, use SQLCipher online backup, copy/link only immutable objects referenced by that DB snapshot, verify destination, then atomically publish the backup directory where the filesystem permits.
5. Document that copying a live directory with generic file tools is not guaranteed consistent. Provide supported cold-copy and application snapshot procedures.
6. Implement restore verification into a new path; never overlay a live vault. Preserve the source on every failure.
7. Replace compaction UI/status with database-size, live-object, garbage, and staging metrics plus Verify/GC/Backup operations.
8. Add structured non-secret progress and error reporting; names/tags/queries/content and keys remain absent from logs.
9. Test low-disk behavior and compute preflight requirements for backup, DB rebuild, object replacement, and conversion.
10. Add periodic maintenance documentation and update the help/settings surfaces.

## TDD and recovery tests

- Backups taken during simulated reads and queued writes reopen to one consistent generation and contain every referenced object.
- Restore never modifies source/destination-on-collision and rejects incomplete/corrupt backups.
- Deep verify detects every injected corruption class; permitted repairs are idempotent.
- GC and derived-object rebuild remain safe under cancellation/crash/disk-full.

## Acceptance criterion

Users can verify, safely garbage-collect, back up, and restore a v3 vault with documented consistency guarantees; injected corruption is accurately classified; no repair causes silent original-media loss.
