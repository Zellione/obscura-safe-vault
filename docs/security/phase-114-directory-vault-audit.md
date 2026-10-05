# Phase 114 directory-vault security and durability review

Scope: the v3 production cutover, including header/database/object storage,
unlock and key teardown, mutation ordering, recovery/GC/backup, filesystem
boundaries, and legacy read-only coexistence. No unresolved high- or
medium-severity issue was found.

| Boundary | Conclusion and evidence |
|---|---|
| Plaintext at rest | `vault.db`, its rollback journal, online backups, and immutable objects are encrypted. Staging contains only encrypted object bytes. The sole intentional plaintext sink remains consent-gated export. Encrypted-artifact and no-plaintext checks live in the v3 DB/object/backup tests. |
| Plaintext memory | Application-owned header credentials, keys, DB-derived text, and decoded object bytes use secure buffers where APIs permit. SQLCipher/SQLite page caches remain opaque third-party allocations; opening the v3 pipeline marks secure memory degraded and F1 reports the limitation. |
| Key lifetime | Unlock derives KEK, unwraps the master key, derives domain-separated DB/object keys, then releases the KEK. Database/session handles close before owning keys are wiped. Password rewrap replaces only the authenticated header. Tests cover wrong credentials, identity binding, teardown, and rewrap. |
| Authentication/substitution | Header wrapping binds format flags and `vault_id`; DB/object keys are vault-domain separated; object AEAD binds vault ID, object ID, owner node, role, format, frame index, and lengths. Wrong vault/owner/role/key, frame swaps, copied objects, and copied databases fail closed. |
| Filesystem confinement | Root and child opens are descriptor-relative with no-follow/type/owner/mode/link-count validation. Object names have one canonical grammar. Tests cover symlink roots/nodes, hard links, root replacement, concurrent publication, unknown entries, bad permissions, and path escapes. |
| Publication order | Immutable object bytes are write/sync/publish/directory-sync completed before the SQL reference commits. Replacement commits the new reference before reclaiming the old object. Header replacement and backup publication use creation-only durable replacement. |
| Crash/recovery | Fault injection covers open/write/file-sync/rename/directory-sync/lock/unlink. The subprocess mutation matrix terminates after injected publication faults, then cold-opens and reconciles from real filesystem state. SQLCipher recovery, header replacement, GC recheck, backup verification, and incomplete restore have dedicated tests. |
| GC races | Reconciliation reads one DB snapshot; collection requires the writer lease and rechecks each candidate against the live DB immediately before unlink. A newly referenced candidate is preserved. |
| Logging and diagnostics | Findings and logs contain status, counts, and opaque IDs only—never credentials, keys, decrypted metadata, or media bytes. |
| Core dumps | Release sets `RLIMIT_CORE=0`. It deliberately does not set `PR_SET_DUMPABLE=0`, because that breaks XDG portal access to `/proc/<pid>/root`. Debug builds retain cores and are documented as unsuitable for sensitive vaults. |
| Legacy boundary | Newly created vaults use v3. Opened legacy files are read-only across every public mutation facade; an explicit test-only seam retains Phase 115 compatibility/crash fixtures. |

## Accepted limitation: whole-directory rollback

An attacker who can replace the entire vault directory with a previously valid
snapshot can roll the vault back without violating any internal authentication
tag. Preventing this requires trusted monotonic state outside the portable
vault (for example, an OS keystore or remote witness), which is outside the
local-attacker/portable-vault model. Mixed-generation substitution is detected:
individual copied databases and objects fail identity or reference binding.

## Residual low-risk limitations

- SQLCipher/SQLite and codec internals cannot accept the application's locked,
  wipe-on-release allocator for every transient plaintext allocation. The UI
  reports degraded secure-memory status whenever these paths are active.
- `mlock` cannot keep pages out of a hibernation image. Users requiring that
  property must disable hibernation or use encrypted swap/hibernation storage.
- A generic live filesystem copy is not a supported backup. The in-app snapshot
  holds the writer barrier, uses SQLCipher online backup, copies referenced
  immutable objects, deeply verifies, and publishes creation-only.
