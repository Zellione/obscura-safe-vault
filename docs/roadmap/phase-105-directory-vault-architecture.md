# Directory-vault architecture and format contract (Phase 105)

**Status:** in progress

## Implementation progress (2026-09-26)

Completed on the Phase 105 branch:

- `docs/V3_VAULT_FORMAT.md` freezes the root/header/object layout, key domains,
  associated-data encoding, publication ordering, compatibility boundary, and
  initial database model.
- ADR 0001 selects pinned SQLCipher 4.19.0 and rejects plaintext SQLite, a
  custom encrypted VFS, and the incompatible SQLCipher 5 beta.
- SQLCipher is a vendored submodule and builds as an out-of-tree static test
  dependency, including a separately instrumented ASAN build.
- The test probe proves raw 32-byte key use, encrypted database and rollback
  journal canaries, wrong-key rejection, and correct-key reopen.
- `v3_crypto_spec.*` implements the frozen database/object key derivations and
  66-byte object-frame AD with exact-byte known-answer tests. Derived keys use
  `SecureBuffer` and transient KDF input is wiped.

Still required before marking the phase complete:

- Commit the format detector and its hostile path/header matrix.
- Freeze executable schema DDL and test constraints/query plans.
- Commit the pure publication crash-state model and exhaustive transition tests.
- Resolve the production crypto-provider pin: the host OpenSSL provider is
  acceptable only for this validation spike; Phase 107 cannot ship with it.
- Run every required configuration and record final counts/results below.

## Goal

Specify the replacement for the single append-only `.osv` file before any production writer is built. A v3 vault is a directory whose secrets are split between one genuinely encrypted SQL database and immutable, independently encrypted object files. This phase is an ADR/specification and dependency-validation phase; it does not change what the application writes by default.

## Proposed on-disk layout

```text
Example.osv/                         # directory, owner-only permissions
  vault.header                      # fixed-size bootstrap; no secret metadata
  vault.db                          # SQLCipher database
  objects/
    7a/7a...f3.osvo                 # immutable encrypted object, opaque ID
    c1/c1...09.osvo                 # two hex chars shard large directories
  staging/                          # same-filesystem unpublished files only
  lock                              # advisory single-writer lock; no secrets
```

`vault.header` contains only magic/version, feature flags, bounded Argon2id parameters, salt, keyfile-required bit, immutable `vault_id`, and the AEAD-wrapped random master key. It is plaintext because password derivation must be bootstrapped before anything can be decrypted, but it contains no vault names, media names, tags, paths, thumbnails, or other user metadata. Its master-key wrap is authenticated with context including the format version and `vault_id`.

`vault.db` is a real SQLite database encrypted by vendored SQLCipher. It owns all logical state: nodes and parent relationships, display names, media properties, object references, tags and category/template data, saved searches, settings, migration/schema versions, and maintenance state. No duplicate plaintext index or sidecar is permitted.

Every application content object is its own authenticated encryption unit. Originals, thumbnails, and posters have distinct random 128-bit `object_id` values and distinct files. One imported video remains one object file, internally divided into authenticated frames for random access. Filenames are lowercase hex object IDs, never user-controlled names. Filesystem size, object count, sharding, and modification timing remain observable; the design does not claim to hide those side channels.

SQLCipher is the baseline choice because implementing a custom encrypted SQLite VFS/page codec would create a new cryptographic storage system. Phase 105 must nevertheless prove in a small spike that the pinned SQLCipher build is static/offline, exception-free at the wrapper, has encrypted database and rollback/WAL artifacts, accepts a raw 256-bit key without applying a second password KDF, and works under ASAN/Release/no-AV builds. If any condition fails, stop for a new owner-approved ADR; do not silently fall back to plaintext SQLite or a home-grown VFS.

## Key hierarchy and identity

1. Derive the KEK exactly from the header's bounded Argon2id parameters and the current domain-separated password/keyfile encoding.
2. Use the KEK only to authenticate and unwrap the random 256-bit master key.
3. Derive a database key from the master key with a fixed domain string, format version, and `vault_id` using the project's keyed BLAKE2b/KDF primitive.
4. Derive each object key from the master key plus a distinct object-key domain, `vault_id`, and random `object_id`.
5. Bind every object preamble and encrypted record to `vault_id`, `object_id`, logical role, owner `node_id`, frame number, and format version. Never bind a mutable filesystem path.
6. Keep KEK, master key, database key, SQL parameter text originating in the database, and decrypted object bytes inside the existing wipe/secure-memory boundary as far as each API permits. Document SQLCipher/SQLite internal allocations as an opaque third-party limitation and surface it in F1 if they cannot be locked/wiped.
7. Password changes generate a new KDF salt and re-wrap the same master key; they do not rewrite the database or objects. Master-key rotation is a separate future operation, not disguised as password change.

## Database model to freeze

The schema design must include, at minimum:

- `vault_meta`: schema/format versions, vault settings, migration watermarks, one logical root ID, and monotonic application generation used for diagnostics—not as an anti-rollback guarantee.
- `nodes`: random stable `node_id`, parent ID, type, secure display name, sibling order, favorite, timestamps, format/dimensions/duration/codec/original size, and constraints preventing self-parenting and invalid types.
- `objects`: `object_id`, role, owning node, encrypted/plain length, frame parameters, content state, and creation generation. A uniqueness constraint prevents one object identity being assigned incompatible roles/owners.
- `tags`, `node_tags`, category/template tables, and tag-field values with explicit case-folding semantics matching current behavior.
- `saved_searches` and settings tables, with bounded blobs/text and no JSON substitute for relational integrity.
- foreign keys enabled on every connection, explicit indexes for gallery listing/tag search/object lookup, and application-side cycle validation because recursive tree constraints do not fit a simple foreign key.

Names and metadata exist only inside encrypted database pages and secure process memory. SQL logs, query traces, error messages, and diagnostics must never emit bound values.

## Atomicity protocol to freeze

There is no atomic transaction spanning SQLite and the filesystem, so correctness comes from ordering and reconciliation:

1. Write a new object to `staging/` with exclusive creation through a root directory handle.
2. Stream-encrypt, flush, `fsync` the file, validate its final authenticated footer/table, and atomically rename it to its final `objects/<shard>/` name without replacement.
3. `fsync` every newly created/renamed directory.
4. Only then commit the SQL transaction that references the object.
5. For replacement, publish new object → atomically switch the DB reference → unlink old unreferenced object.
6. For deletion, remove the DB reference first → commit → unlink the now-unreferenced object.

A crash may therefore leave an unreferenced encrypted object, never a committed row pointing to an incompletely published object. Startup/maintenance reconciliation deletes validated staging debris and garbage-collects unreferenced objects after a conservative grace period. A committed row whose object is missing or unauthentic is corruption and must be reported, never silently removed.

## Step-by-step work

1. Write `docs/V3_VAULT_FORMAT.md` with byte-exact header/object encodings, integer endianness, maximum sizes/counts, SQLCipher parameters, schema DDL, KDF domains, AEAD associated-data bytes, and forward-compatibility rules.
2. Add an ADR comparing SQLCipher against plain SQLite plus custom VFS and against retaining the binary index. Record dependency/license/CVE/update consequences.
3. Build a throwaway test target proving raw-key SQLCipher open/create/reopen, wrong-key failure, encrypted-at-rest inspection, journal recovery, and corruption reporting. Do not connect it to `Vault` yet.
4. Define format detection: regular file with legacy magic is v1/v2; directory with valid `vault.header` is v3; ambiguous/mixed paths fail closed.
5. Define limits before allocation for header values, database rows/text/blobs, object record sizes/counts, video seek tables, and total arithmetic overflow.
6. Specify single-writer/multi-reader process behavior, lock ownership, read connection policy, busy timeout, cancellation boundaries, and lock-before-key-wipe shutdown ordering.
7. Specify compatibility: phases 106–110 are internal foundations; phase 111 adds v3 reads; phase 112 enables v3 writes; phase 114 makes v3 default; legacy writes become disabled only at cutover; phase 115 is the converter.
8. Write a crash-point matrix for create/import/edit/delete/password change/database checkpoint/GC, plus expected cold-open state at every interruption.
9. Threat-model copied/swapped objects, copied databases, symlink replacement, path traversal, malicious SQL pages, rollback to an older whole-vault backup, disk-full, partial writes, and hostile legacy input. Explicitly retain the limitation that whole-directory rollback cannot be detected without an external trusted counter.
10. Review the design with the owner and freeze constants/names only after approval. Later phases may refine implementation, but any cryptographic or durability contract change requires updating this spec first.

## Tests written before implementation

- SQLCipher raw-key known fixture and wrong-key/tampered-page rejection.
- Database/journal files contain none of a set of canary names/tags/searches.
- Exact-byte KDF-domain and object-associated-data vectors.
- Format detector matrix for regular files, directories, symlinks, truncation, wrong magic, and future versions.
- Schema DDL tests for foreign keys, uniqueness, bounds, query plans, and cascade/restrict behavior.
- Model/property tests for every crash transition in the publication protocol.

## Acceptance criterion

The owner has approved the v3 ADR and byte-level format/schema specification; the SQLCipher spike satisfies static/offline build, raw-key, encrypted-artifact, recovery, and sanitizer requirements; all new specification tests pass; and no production vault-writing behavior has changed.
