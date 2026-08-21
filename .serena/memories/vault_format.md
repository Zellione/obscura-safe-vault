# Vault container format (`.osv`)

```
[ Header — plaintext, fixed-size ]
  magic     "OSVAULT\0"  (8 bytes)
  version   u16
  hdr_size  u16
  flags     u32            (bit 0 = framed chunks; bit 1 = domain-separated KDF input)
  KDF block:
    algo        u8  (0 = Argon2id)
    t_cost      u32
    m_cost_kib  u32
    parallelism u32
    salt        u8[16]
    keyfile_req u8
  Master-key wrap (XChaCha20-Poly1305):
    nonce               u8[24]
    wrapped_master_key  u8[32]
    tag                 u8[16]
  Index slot A: offset u64 | length u64 | nonce u8[24]   ─┐ double-buffered
  Index slot B: offset u64 | length u64 | nonce u8[24]   ─┘ crash-safe swap
  active_slot  u8
  [reserved padding to fixed header_size]

[ Data region — append-only ]
  Each chunk: nonce u8[24] | ciphertext (≤1 MiB) | tag u8[16]

[ Index blobs (in data region) — encrypted, binary-serialised tree ]
```

## Index versions

**INDEX_VERSION = 12** (Phase 75): a **thumbnail-budget watermark**, one u16
appended at the very END of the settings block (after the Phase 73
tag-field-values block — the field is last so version-gated reads stay
prefix-stable):

```
migrated_thumb_side  u16 (LE)   # stored-thumb long side the vault's thumbs
                                # were last generated at; 0 = legacy
```

Pre-v12 blobs read `0` and are *offered* one-time thumbnail regeneration at
unlock (MigrationJob thumb arm). Every u16 value is legal — the
reject-not-clamp rule applies to `migrated_index_version`, NOT here.
`Vault::create` stamps `migrated_thumb_side = image::THUMB_MAX_SIDE` (512)
AND `migrated_index_version`, so fresh vaults owe no migration (an unstamped
fresh vault used to get a bogus "sharpen thumbnails" offer). Cross-vault
transfers lower the destination's watermark to the source's when the source
is behind (`lower_dst_watermark`, mirroring the Phase 65 rule). Staleness:
`migrated_thumb_side < image::THUMB_MAX_SIDE` (caller passes the constant —
vault/ stays decoupled the same way probe caps are passed in).

**INDEX_VERSION = 11** (Phase 73): category-template **fields** appended to
each category entry (field sub-block per category) plus a vault-global
**tag-field-values block** appended after the migration watermark
(`{tag, field, value}` entries, matched case-insensitively, single value per
(tag, field)). Pre-v11 blobs read with no templates and no values; oversized
counts/lengths are rejected on deserialise, not clamped.

**INDEX_VERSION = 10** (Phase 65): a **migration watermark**, serialised at the tail
of the Phase 49 vault-global settings block (after category list):

```
migrated_index_version  u8
migrated_probe_caps     u16 (LE)
```

Pre-v10 blobs read with watermark `0/0` (never migrated). Out-of-range watermark
bytes are **rejected on deserialise, not clamped**. Staleness is computed against
`vault::MIGRATION_INDEX_VERSION` (= 7, the version introducing `animated`) and
`media::PROBE_CAPS_GEN` (= 1, current decode capability), NOT against `INDEX_VERSION`
itself — a future sort field or per-node flag would not re-trigger the migration.

**INDEX_VERSION = 9** (Phase 51): a **tag-descriptions sub-block**, serialised
after the Phase 49 vault-global settings block:

```
desc_count       u16   (<= INDEX_MAX_TAG_DESCRIPTIONS = 4096)
descriptions     { name_len u16 (<= INDEX_MAX_TAG_DESC_BYTES = 512);
                   name u8[name_len];
                   desc_len u16 (<= INDEX_MAX_TAG_DESC_BYTES = 512);
                   desc u8[desc_len] } [desc_count]
```

Pre-v9 blobs read with an empty descriptions list. An oversized count or an
out-of-range name/desc length is **rejected on deserialise, not clamped** —
the Phase 37 rule. The writer clamps; the reader rejects. Fuzzed by
`test_fuzz.cpp`'s mutation harness, whose base blob is built with the
4-argument `serialize_index` so description bytes are reachable by mutation.

**INDEX_VERSION = 8** (Phase 49): a **vault-global settings block**, serialised
after the Phase 18 saved-searches block (vault-level metadata, not part of any
node):

```
default_sort     u8    (SortKey; Insertion for pre-v8 blobs)
tiles_show_tags  u8    (0/1;     1         for pre-v8 blobs)
cat_count        u16   (<= INDEX_MAX_TAG_CATEGORIES = 256)
categories       { name_len u16 (<= INDEX_MAX_CATEGORY_BYTES = 64);
                   name u8[name_len];
                   swatch u8 (< TAG_SWATCH_COUNT = 16) } [cat_count]
```

Pre-v8 blobs read with `Insertion`, tile tags on, and `VaultSettings::seeded()`
(8 categories). An out-of-range swatch/sort/flag byte or an oversized count is
**rejected on deserialise, not clamped** — the Phase 37 rule. The writer clamps;
the reader rejects. Fuzzed by `test_fuzz.cpp`'s mutation harness, whose base blob
is built with the 4-argument `serialize_index` so category fields are reachable
by byte mutation.

The same bump reworks `SortKey`: byte `0` is re-read as `Default` ("follow the
vault default") and `7 = Insertion` is added for raw import order, so existing
galleries adopt the vault default with **no migration**. `read_node` bounds
`sort_key` per version (v6/v7 max 6, v8 max 7).

**INDEX_VERSION = 7** (Phase 47): Image nodes carry an `animated u8` flag
(0=static, 1=animated) after `thumb_length`. v1–v6 blobs read as false.
Bytes other than 0/1 are rejected on deserialise (not clamped), matching the
Phase 37 `sort_key` rule. Lazy repair via `Vault::repair_image_animated` heals
pre-v7 GIFs on first view.

The flag is **format-neutral**, which is why Phase 57 added animated WebP with
**no version bump at all**: only which formats the writer/reader consult
(`vault::format_can_animate` — GIF and WebP) changed, not the layout. A vault
written before Phase 57 cannot contain an animated WebP, because such a file
failed to decode and was rejected at import.

## Key hierarchy

New vaults derive `KEK = Argon2id("OSV-KDF-INPUT-2" || len(password) ||
len(keyfile) || password || keyfile, salt)`, with both lengths encoded as little-endian
u64. Header flag bit 1 selects this encoding. When clear, readers use the original
`password || keyfile` concatenation for compatibility; a successful password change
rewraps the same master key using the new encoding and sets bit 1. KDF headers are
bounded before allocation to 256 MiB, 10 passes, and 16 lanes. The Argon2 workspace is
best-effort locked and always wiped. The KEK unwraps a random 32-byte
**master key**. All data/thumbnail/index chunks use the master key with a
fresh random 24-byte nonce per chunk.

## Write atomicity

Append chunks → fsync → write index to inactive slot → fsync → flip
`active_slot` → fsync. A crash before the flip leaves the previous index
valid; orphaned chunks are reclaimed either by `compact()` (Phase 60: in-place
dead-space packing — live ciphertext moved into dead gaps with batched slot-swap
commits, final blob placed low, dead tail truncated; shrinks logical size with
O(1) extra disk, crash-safe because no move ever overwrites a byte the
last-committed index references) or, on Linux, by `reclaim()`
which punches holes over the dead spans in place — offset-stable, no temp copy,
no disk spike, so the file just goes sparse (logical size unchanged). See
`mem:module/vault` "Reclamation".

## See also

Index tree serialisation (`IndexNode`, tags, favorites, video metadata,
saved searches, sort keys, tag descriptions) and the framed-chunk compression codec: `mem:core`
(vault/ section — `index.*`, `chunk_codec.*`, `index_io.*`).
