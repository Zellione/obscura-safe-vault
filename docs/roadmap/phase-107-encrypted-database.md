# Encrypted database foundation and schema (Phase 107)

**Status:** complete

## Delivered

- Production targets now link the pinned static SQLCipher/OpenSSL build rather
  than limiting it to the Phase 105 validation spike.
- `vault::v3::Database` provides the move-only, no-exception boundary:
  raw-key setup, encrypted rollback journal + FULL synchronous mode, defensive
  and untrusted-schema configuration, bounded SQLite limits, schema-v1
  creation, immediate key/schema verification, integrity checking, and typed
  wrong-key/corruption, busy, disk-full, constraint, and future-version status.
- Prepared-statement repository operations cover typed node insert/list/lookup/
  search, object-reference snapshots, tags and assignments, category templates,
  tag field values/descriptions, saved searches, and settings. Decrypted text
  and query blobs return in `SecureString`/`SecureBlob` owners.
- The schema boundary rejects future and pre-v1 versions. Schema v1 is the
  initial supported minimum, so there is no historical encrypted schema to
  transform; later upgrades must add a transactional step before raising
  `SCHEMA_VERSION`. Creation is one transaction and cleans up on failure.
- Online backup uses SQLCipher's backup API into an exclusively created,
  encrypted same-directory staging database, verifies integrity, fsyncs the
  file, publishes without replacement, and fsyncs the directory.
- TDD coverage proves Unicode mixed metadata round-trip, search, every v1
  settings/tag/template field, object snapshots, encrypted primary/backup
  canaries, backup reopen, wrong-key fail-closed, future-version rejection,
  constraints, and close-before-removal. Phase 105's retained tests cover
  encrypted rollback-journal canaries, page corruption, and frozen query plans.
- Gates: 2,307 Debug tests, 2,307 Release tests, 2,307 ASAN/UBSan tests, and
  2,113 no-FFmpeg tests pass with zero failures. No `.osv` or v3 format change.

## Goal

Introduce the encrypted `vault.db` as the sole authoritative store of filenames, gallery structure, metadata, tags, saved searches, and settings.

## Step-by-step work

1. Vendor and pin SQLCipher and its required crypto dependency per the approved Phase 105 ADR; document licenses, build flags, disabled extensions, update procedure, and CVE cadence.
2. Add a narrow `vault/db` wrapper: prepared statements only, no exceptions across the boundary, typed status mapping, disabled extension loading, bounded limits, defensive/trusted-schema settings, foreign keys always on, and no SQL tracing with values.
3. Derive the raw database key from the unlocked master key; pass it without hex/string copies where the API permits; wipe all temporary key material; verify the key immediately with a schema query before accepting unlock.
4. Select and freeze journal mode and `synchronous` settings from the Phase 105 crash tests. Confirm every database/journal artifact is encrypted and durable before allowing production data.
5. Implement schema creation exactly from the spec: metadata, nodes, objects, tags, joins, category/template/value tables, saved searches, and settings, including bounds and indexes.
6. Add a transactional schema migration framework with `user_version`, supported min/max versions, preflight free-space checks, rollback on failure, and rejection of future versions.
7. Implement repository operations for tree listing, lookup, search, tags, settings, and object-reference snapshots. Keep SQL rows out of UI code and return secure string/blob types.
8. Add connection ownership rules: one serialized writer; bounded read connections or snapshots for workers; statements finalized before lock; database handles closed before database/master keys are wiped.
9. Map corruption, wrong key, busy, disk full, constraints, and unsupported version to distinct non-secret user-facing errors.
10. Add a database backup primitive using SQLCipher's supported online backup path into an exclusively created encrypted destination, followed by integrity verification and durable publication.

## TDD and security tests

- Full schema round-trip covering Unicode names, nested mixed galleries, all metadata/tag/template/settings fields, and maximum legal lengths/counts.
- Constraint tests for dangling parents/objects, duplicate identity, invalid enum/range values, and future schema versions.
- At-rest canary scan across DB, journal, temporary, and backup files.
- Wrong database key, copied DB from another `vault_id`, corrupted page, interrupted transaction, and disk-full tests.
- Query-plan assertions for 50k/250k-node gallery, tag, and object-reference queries.
- Lock teardown proves no statement/connection survives key wipe.

## Acceptance criterion

An isolated v3 database can be created, populated, queried, backed up, reopened, and migrated using only its derived key; all secret metadata is encrypted at rest; corrupt/wrong-key databases fail closed; and all required test configurations pass.
