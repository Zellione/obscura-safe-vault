# ADR 0001: SQLCipher database for directory vault metadata

- Status: accepted for the v3 format
- Date: 2026-09-26
- Decision owner: project owner, through the merged Phase 105 roadmap

## Context

The legacy vault stores encrypted media records and a serialized metadata tree in one append-only file. The v3 design needs a directory of independently encrypted media objects and a real database that owns filenames, hierarchy, metadata, tags, saved searches, and settings. The database must not expose those values at rest, including in recovery artifacts.

## Decision

Use SQLCipher 4.19.0, pinned as `vendor/sqlcipher`, as the SQLite implementation for `vault.db`. Version 5.0.0 is a beta with incompatible defaults and is not suitable for a durable production format. SQLCipher is built statically, with loadable extensions disabled, temporary SQLite storage forced into memory, and pinned OpenSSL 3.5.8 LTS as a static `libcrypto` provider. OpenSSL is built without shared libraries, TLS (`libssl`), applications, dynamic modules, engines, the legacy provider, documentation, or its upstream test programs; this keeps the provider surface to the primitives SQLCipher consumes.

The application supplies a random, domain-derived 256-bit database key through
`sqlite3_key`. SQLCipher requires direct keys in its 67-byte `x'<64 hex>'`
keyspec form, so the application constructs that transient representation only
in locked, wipe-on-release memory. It is passed through the C API, never
interpolated into SQL. SQLCipher's password KDF is therefore not part of the
vault password flow. The user's password/keyfile derives only the KEK used to
unwrap the vault master key. SQLCipher retains its random 16-byte database salt
and per-page authentication.

The database uses an encrypted header (`cipher_plaintext_header_size = 0`). Rollback-journal mode is the initial durability baseline because it has the smallest set of persistent sidecars and the Phase 105 probe proves that rollback journal page images are encrypted. WAL may be adopted only through a later format-neutral performance decision after equivalent crash, encrypted-artifact, backup, and checkpoint tests.

`PRAGMA cipher_memory_security = ON`, foreign keys, defensive mode, trusted-schema restrictions, bounded SQLite limits, prepared statements, and disabled extension loading are mandatory in the Phase 107 wrapper. SQL text and bound values must never be traced or logged.

## Alternatives considered

### Plain SQLite plus an application-encrypted metadata blob

Rejected. It would not be a relational encrypted database, would preserve the monolithic-index failure mode, and would require application code to reproduce transactions, indexing, migrations, and concurrent snapshots.

### Custom encrypted SQLite VFS or page codec

Rejected. Correctly handling page numbers, journals, WAL, salts, authentication, torn writes, backups, and SQLite version behavior would create a new cryptographic storage subsystem with a much larger audit burden than using SQLCipher.

### SQLCipher 5 beta

Rejected for v3 format freeze. It changes the codec architecture and defaults and explicitly warns against production use. A future upgrade requires its own compatibility and migration design.

### Separate encrypted file for every metadata row

Rejected. It would make relational queries and atomic tree/tag mutations difficult, produce excessive filesystem fan-out, and still require a transactional index.

## Crypto-provider boundary

OpenSSL 3.5.8 LTS is pinned as `vendor/openssl` and supported upstream through April 2030. `scripts/build_openssl.sh` produces only a static `libcrypto.a`; SQLCipher's static archive is compiled against those vendored headers and the final link names the vendored archive explicitly. CI checks and the Phase 105 verification use `ldd` to reject an unintended dynamic SQLCipher, SQLite, OpenSSL, or libcrypto dependency. Both normal and sanitizer builds have separate output prefixes.

## Consequences

- The database and its recovery/backup artifacts receive SQLCipher's page encryption and authentication.
- SQLite/SQLCipher internal plaintext allocations are opaque third-party memory. The wrapper enables SQLCipher memory security, minimizes connection/cache lifetime, closes all handles before key wipe, and reports this limitation through the existing secure-memory degraded status.
- The database file's size and modification timing remain observable.
- SQLCipher becomes an untrusted-input parser and enters the quarterly CVE review cadence.
- Backups must use the SQLCipher-aware online backup path; generic live directory copies are not promised to be consistent.
