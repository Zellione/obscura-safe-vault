# Directory-vault backup and recovery

Directory vaults are a coordinated set of encrypted files. Do not use a generic live-directory
copy as a backup: the copied SQLCipher generation may reference objects that were not copied yet,
or may omit objects copied from a later generation.

## Supported procedures

- **Application snapshot:** keep the vault unlocked and use the backup operation. It takes the
  vault's writer barrier, authenticates the database and every referenced object frame, copies
  only immutable objects referenced by the SQLCipher snapshot, verifies the completed copy, and
  publishes the destination directory without replacing an existing path.
- **Cold copy:** fully exit Obscura Safe Vault, confirm no process holds the vault's `lock` file,
  and copy the whole directory while it remains offline. Verify the copy with the application
  before treating it as a usable backup.
- **Restore:** restore to a new, non-existing path. The application authenticates the source
  header, database, and every referenced object, then creates and verifies a new directory. It
  never overlays a live vault and never modifies the backup source.

Back up the complete directory. `vault.header`, `vault.db`, `objects/`, `staging/`, and `lock` are
one storage unit; an object directory or database alone is not a backup.

## Maintenance and findings

Quick verification checks the encrypted database and referenced-file presence/length. Deep
verification authenticates every referenced object's encrypted metadata, table, and frame.
Findings are intentionally separated:

- a missing, wrong-sized, malformed, or unauthentic referenced object is corruption and is never
  deleted automatically;
- a valid unreferenced immutable object is garbage and may be collected after the grace period;
- stale application-owned staging files may be removed under the writer lease;
- unknown names, links, ownership, modes, or future layout entries fail layout validation and
  must be investigated rather than silently removed.

Garbage collection rechecks each candidate against the live database immediately before unlink.
Cancellation can leave already-published but unreferenced encrypted objects; a later scan safely
rediscovers them. Keep at least the reported live-object bytes plus the encrypted database size
free for a snapshot, with additional filesystem overhead. Low-disk failures leave the source and
any pre-existing destination untouched.

Recommended cadence: run quick verification at open, deep verification before and after moving a
backup, and garbage collection after large deletions/import cancellations. Keep multiple offline
backup generations; whole-directory rollback cannot be detected without an external trusted
counter.

## In-app operations

Open **F2 → Vault**, or press **Shift+C** from a directory vault. The panel reports encrypted
database, live-object, and garbage sizes and offers deep verification, safe garbage collection,
encrypted backup, derived thumbnail/poster rebuild, and database optimization. Long operations
run behind an exclusive progress modal: imports, screen reads, idle locking, and shutdown cannot
race the vault while the writer barrier is held. Operation messages contain counts, byte totals,
and typed outcomes only—never names, tags, queries, plaintext, credentials, or keys.

Backup preflight reserves the referenced object bytes, two encrypted database images (destination
plus journal/rewrite headroom), and 1 MiB for control files/filesystem overhead. If that space is
not available, no destination is created. Database optimization uses `PRAGMA optimize`, a
lossless encrypted `VACUUM`, and a full integrity/foreign-key check before reporting success.
