# V3 directory vault format

Status: Phase 105 format contract. Production writing begins in later phases.

All integers are unsigned little-endian. All reserved bytes must be zero when written and rejected when a feature-bearing reader cannot safely ignore them. Every count, offset, and length is checked for overflow and against the limits below before allocation or I/O.

## Root layout

```text
<name>.osv/
  vault.header
  vault.db
  objects/<id[0:2]>/<id>.osvo
  staging/
  lock
```

The root and directories are mode `0700`; regular files are mode `0600`. All access is relative to an open root directory descriptor with no-follow/beneath resolution. User-controlled names never become paths. Object IDs are 16 random bytes rendered as exactly 32 lowercase hexadecimal characters. Shards are exactly the first two characters.

`vault.header` is the sole plaintext bootstrap file. It contains no user metadata. `vault.db` is SQLCipher-encrypted and is the sole authority for names, tree structure, metadata, tags, searches, settings, and object references. Object files are immutable after publication.

## `vault.header` (256 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | magic `OSV3DIR\0` |
| 8 | 2 | format version, `3` |
| 10 | 2 | header size, `256` |
| 12 | 4 | feature flags; initially zero |
| 16 | 1 | KDF algorithm; `0` = Argon2id |
| 17 | 4 | Argon2id time cost |
| 21 | 4 | Argon2id memory KiB |
| 25 | 4 | Argon2id parallelism |
| 29 | 16 | random KDF salt |
| 45 | 1 | keyfile required, exactly `0` or `1` |
| 46 | 24 | master-key-wrap nonce |
| 70 | 32 | wrapped random master key |
| 102 | 16 | master-key-wrap Poly1305 tag |
| 118 | 16 | immutable random `vault_id` |
| 134 | 122 | zero reserved bytes |

KDF input remains the current domain-separated length-prefixed password/keyfile encoding. Bounds are 256 MiB memory, 10 passes, and 16 lanes before allocation. The wrap associated data is:

```text
domain u8 = 0x10 | AD version u8 = 1 | format u16 = 3 |
vault_id[16] | header_size u16 = 256 | flags u32
```

Password/keyfile change writes a new salt/KDF block and wrap for the same master key through the durable header-replacement protocol. It does not rewrite the database or objects.

## Derived keys

Derivations use keyed BLAKE2b with the 32-byte master key as key and a 32-byte output.

```text
database key input = ASCII `OSV-V3-DB-KEY` | 0x00 | vault_id[16] | u16(3)
object key input   = ASCII `OSV-V3-OBJECT-KEY` | 0x00 |
                     vault_id[16] | object_id[16] | u16(1)
```

The database key is supplied as 32 raw bytes to `sqlite3_key`; it is never formatted into SQL. Each object has a distinct derived key. Domain strings and terminators are normative bytes, not C strings.

## SQLCipher database

The pinned baseline is SQLCipher 4.19.0 with encrypted header, 4096-byte cipher pages, page HMAC enabled, raw-key API, in-memory temporary storage, memory security enabled, rollback journal, and `synchronous=FULL`. A later journal-mode change does not change the v3 vault format but must pass equivalent encrypted-artifact and crash tests.

### Initial schema model

- `vault_meta`: exactly one row; schema version, logical root ID, application generation, settings and migration watermarks.
- `nodes`: 16-byte primary ID, nullable parent, node type, display name, sibling order, favorite, timestamps, format/dimensions/duration/codec/original size. The root is the only row with no parent.
- `objects`: 16-byte primary ID, owning node, role, encrypted/plain lengths, frame size/count, creation generation, and state. `(node_id, role)` is unique where the role is singular.
- `tags` and `node_tags`: canonical case-insensitive identity plus preserved display spelling and many-to-many assignment.
- category, template-field, tag-field-value, saved-search, and settings tables corresponding to every current index-v13 field.

All connections enable foreign keys. Deleting an original node cannot cascade silently over an inconsistent object set. Tree-cycle validation is application-enforced inside the same writer transaction. Text/blob/count limits are checked both at the wrapper and with schema constraints where practical. Exact DDL is frozen in Phase 107 alongside repository queries and query-plan tests.

## Object file (`.osvo`)

### Plaintext preamble (64 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | magic `OSVOBJ\0\0` |
| 8 | 2 | object format version, `1` |
| 10 | 2 | preamble size, `64` |
| 12 | 4 | flags; initially zero |
| 16 | 16 | `vault_id` |
| 32 | 16 | `object_id` |
| 48 | 4 | maximum plaintext frame bytes |
| 52 | 12 | zero reserved bytes |

The preamble is authenticated as part of every encrypted record. Its object ID must equal the filename and shard. It intentionally reveals only identity/framing data; filesystem size already reveals approximate content size.

### Encrypted records

The first record is an encrypted object header containing role, owner `node_id`, media format, exact plaintext size, frame count, compression method, and seek-table description. It is followed by independently encrypted data frames and a final authenticated table/footer. Each record is:

```text
nonce[24] | ciphertext[declared plaintext length] | tag[16]
```

The canonical 66-byte associated data is:

```text
role u8 | AD version u8 = 1 | vault_id[16] | object_id[16] |
owner node_id[16] | frame_index u32 | frame_count u32 |
total_plaintext_length u64
```

Roles are `1=original image`, `2=thumbnail`, `3=poster`, `4=original video`, with zero and 255 reserved. Header and footer/table records use reserved logical frame indexes outside the data range and distinct role-domain bytes defined when Phase 108 freezes their exact encoding. A reader validates preamble, authenticated header, all arithmetic/table bounds, and the requested frame tag before releasing plaintext.

Images may use one or more frames. A video is one object file with fixed-size independently authenticated frames, allowing bounded random access. Compression, where used, occurs before encryption per frame and carries an authenticated decoded-size limit.

## Publication and crash consistency

1. Exclusively create a random file under `staging/`.
2. Stream and authenticate the complete object; flush and `fsync` it.
3. Validate its final table/footer, then rename without replacement to its final shard path.
4. `fsync` newly created/renamed directories.
5. Only then commit the SQL transaction that references the object.

Replacement publishes new → switches the database reference → removes old. Deletion removes the database reference → commits → removes the old file. A crash can leave garbage but cannot legitimately commit a reference to a partial/unpublished object. Missing or unauthentic referenced objects are corruption, never silently garbage-collected.

## Limits

- Header size: exactly 256 for format 3.
- IDs: exactly 16 bytes; all-zero IDs invalid.
- Node-name component: current 255-byte safe-name limit.
- Tree depth: current application recursion budget, stored and checked explicitly.
- Object frame plaintext: at most 1 MiB in format 1 unless a future feature flag specifies another bounded profile.
- Individual metadata strings/counts: no greater than current index-v13 limits; Phase 107 DDL names each bound.
- SQLite length, column, expression-depth, attached-database, and page-count limits are reduced from permissive defaults by the wrapper before untrusted queries/pages are processed.

## Detection and compatibility

- A regular file beginning `OSVAULT\0` is a legacy candidate.
- A real directory containing a real regular `vault.header` beginning `OSV3DIR\0` is a v3 candidate.
- Symlinks, mixed file/directory shapes, truncated headers, unsupported versions, unexpected required-file types, or both signatures fail closed.
- Phases 111–114 retain the legacy reader. Phase 114 makes legacy sessions read-only. Phase 115 performs copy-based conversion and never modifies the legacy source.

Whole-directory rollback to a self-consistent older backup is not detectable without an external trusted monotonic anchor and remains an accepted limitation.

