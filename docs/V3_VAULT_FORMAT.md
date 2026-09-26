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

SQLCipher's C API recognizes a direct key only in its 67-byte `x'<64 hex>'`
keyspec representation. The application encodes the 32 derived bytes into that
form in locked, wipe-on-release memory and passes it directly to `sqlite3_key`;
it never interpolates the key into SQL. This selects SQLCipher's raw-key path
and avoids a second password KDF. Each object has a distinct derived key. Domain
strings and terminators are normative bytes, not C strings.

## SQLCipher database

The pinned baseline is SQLCipher 4.19.0 with encrypted header, 4096-byte cipher pages, page HMAC enabled, raw-key API, in-memory temporary storage, memory security enabled, rollback journal, and `synchronous=FULL`. A later journal-mode change does not change the v3 vault format but must pass equivalent encrypted-artifact and crash tests.

### Initial schema model

- `vault_meta`: exactly one row; schema version, logical root ID, application generation, settings and migration watermarks.
- `nodes`: 16-byte primary ID, nullable parent, node type, display name, sibling order, favorite, timestamps, format/dimensions/duration/codec/original size. The root is the only row with no parent.
- `objects`: 16-byte primary ID, owning node, role, encrypted/plain lengths, frame size/count, creation generation, and state. `(node_id, role)` is unique where the role is singular.
- `tags` and `node_tags`: canonical case-insensitive identity plus preserved display spelling and many-to-many assignment.
- category, template-field, tag-field-value, saved-search, and settings tables corresponding to every current index-v13 field.

All connections enable foreign keys. Deleting an original node cannot cascade silently over an inconsistent object set. Tree-cycle validation is application-enforced inside the same writer transaction. Text/blob/count limits are checked both at the wrapper and with schema constraints where practical. The normative initial DDL is `vault::v3::SCHEMA_SQL` in `src/vault/v3_schema.h`; Phase 107 must change it only through a versioned migration. Phase 105 tests its foreign keys, identity/role/root/tag constraints, schema version, and gallery/tag query plans.

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

The file order is preamble, fixed-size encrypted header record, data records,
encrypted seek-table record, and fixed-size encrypted footer record. Each record is:

```text
nonce[24] | ciphertext[declared plaintext length] | tag[16]
```

The canonical 66-byte associated data is:

```text
role u8 | AD version u8 = 1 | vault_id[16] | object_id[16] |
owner node_id[16] | frame_index u32 | frame_count u32 |
total_plaintext_length u64
```

Roles are `1=original image`, `2=thumbnail`, `3=poster`, `4=original video`, with zero and 255 reserved. The associated-data `frame_index` values are `0..frame_count-1` for data, `0xffffffff` for the header, `0xfffffffe` for the seek table, and `0xfffffffd` for the footer. These values are never valid data-frame indexes. A reader validates the preamble, authenticated header, all arithmetic/table bounds, and the requested frame tag before releasing plaintext.

The encrypted header plaintext is exactly 64 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | record kind, `0` |
| 1 | 1 | object role |
| 2 | 1 | media format code |
| 3 | 1 | object codec flags; initially zero |
| 4 | 16 | owner `node_id` |
| 20 | 8 | total plaintext length |
| 28 | 4 | maximum plaintext frame bytes |
| 32 | 4 | data frame count |
| 36 | 28 | zero reserved bytes |

The seek-table plaintext begins with `table_version u16 = 1`, `entry_size u16 = 24`, and `entry_count u32`, followed by one entry per data frame:

```text
record_offset u64 | record_length u64 | plaintext_length u32 |
compression u8 (0=raw, 1=deflate) | reserved[3]=0
```

Offsets are from the beginning of the object file. Entries are strictly increasing, non-overlapping, wholly before the table, and cover exactly `frame_count` frames whose plaintext lengths sum to the authenticated total. Decompression must produce exactly the declared plaintext length.

The encrypted footer plaintext is exactly 64 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | record kind, `2` |
| 1 | 1 | object role |
| 2 | 2 | footer version, `1` |
| 4 | 4 | frame count |
| 8 | 8 | seek-table record offset |
| 16 | 8 | seek-table record length |
| 24 | 8 | total object-file length |
| 32 | 32 | keyed BLAKE2b digest of authenticated header plus seek-table plaintext |

The digest uses the object key, a 32-byte output, and this exact input:

```text
ASCII `OSV-V3-OBJECT-TABLE` | 0x00 | authenticated header plaintext[64] |
seek-table plaintext[variable]
```

Because the footer plaintext is fixed at 64 bytes, its complete encrypted record is the final 104 bytes of the file and can be found from EOF. The digest is defense-in-depth over table/header agreement; AEAD authentication remains authoritative.

Images may use one or more frames. A video is one object file with independently authenticated frames, allowing bounded random access through the authenticated seek table. Compression occurs before encryption per frame and carries an authenticated decoded-size limit.

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
- Tree depth: at most 128 edges from the logical root.
- Object frame plaintext: at most 1 MiB in format 1 unless a future feature flag specifies another bounded profile.
- At most 4,096 tags and saved searches, 256 tag categories, 16 fields per
  category, 16,384 assigned tag-field values, and 1,048,576 video frames.
- Category/field names are at most 64 UTF-8 bytes; field values 256 bytes; tag
  descriptions 512 bytes; saved-search query blobs 1 MiB. Phase 107 enforces
  these wrapper-level aggregate limits in addition to schema constraints.
- SQLite length, column, expression-depth, attached-database, and page-count limits are reduced from permissive defaults by the wrapper before untrusted queries/pages are processed.

## Concurrency and shutdown

The `lock` file admits exactly one writable process. A writer owns one serialized
write connection; read-only connections use bounded busy timeouts and never
upgrade themselves into writers. Object publication and garbage collection run
only while the writer lock is held. Cancellation is observed between durable
publication steps, never inside an `fsync`, rename, or database commit. Shutdown
first stops and joins work, closes all database connections and object handles,
releases the process lock, and only then wipes database/master keys.

## Threat model notes

- Database pages, journals, object files, headers, paths, and directory entries
  are hostile input. SQLite defensive/trusted-schema settings, limits, prepared
  statements, authenticated object identities, and descriptor-relative
  no-follow traversal form the validation boundary.
- Copying or swapping a database/object from another vault fails because keys
  and associated data bind the immutable `vault_id` and object/node identity.
  Swapping an older object within the same vault fails when authenticated
  identity or committed length/generation metadata disagrees.
- Disk-full and partial writes may leave staging files or unreferenced encrypted
  objects. Publish-before-reference ordering prevents those files from becoming
  committed live content.
- User names never become paths. Legacy containers remain untrusted input and
  are converted by copying through the validated legacy reader.

## Detection and compatibility

- A regular file beginning `OSVAULT\0` is a legacy candidate.
- A real directory containing a real regular `vault.header` beginning `OSV3DIR\0` is a v3 candidate.
- Symlinks, mixed file/directory shapes, truncated headers, unsupported versions, unexpected required-file types, or both signatures fail closed.
- Phases 111–114 retain the legacy reader. Phase 114 makes legacy sessions read-only. Phase 115 performs copy-based conversion and never modifies the legacy source.

Whole-directory rollback to a self-consistent older backup is not detectable without an external trusted monotonic anchor and remains an accepted limitation.
