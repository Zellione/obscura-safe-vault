# Vault container format (`.osv`)

```
[ Header — plaintext, fixed-size ]
  magic     "OSVAULT\0"  (8 bytes)
  version   u16
  hdr_size  u16
  flags     u32            (bit 0 = chunk/index plaintext framed, Phase 26)
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
(0=static, 1=animated GIF) after `thumb_length`. v1–v6 blobs read as false.
Bytes other than 0/1 are rejected on deserialise (not clamped), matching the
Phase 37 `sort_key` rule. Lazy repair via `Vault::repair_image_animated` heals
pre-v7 GIFs on first view.

## Key hierarchy

`KEK = Argon2id(password [‖ keyfile_bytes], salt)` → unwraps a random 32-byte
**master key**. All data/thumbnail/index chunks use the master key with a
fresh random 24-byte nonce per chunk.

## Write atomicity

Append chunks → fsync → write index to inactive slot → fsync → flip
`active_slot` → fsync. A crash before the flip leaves the previous index
valid; orphaned chunks are reclaimed by compaction.

## See also

Index tree serialisation (`IndexNode`, tags, favorites, video metadata,
saved searches, sort keys, tag descriptions) and the framed-chunk compression codec: `mem:core`
(vault/ section — `index.*`, `chunk_codec.*`, `index_io.*`).
