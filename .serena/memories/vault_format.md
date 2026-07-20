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
saved searches, sort keys) and the framed-chunk compression codec: `mem:core`
(vault/ section — `index.*`, `chunk_codec.*`, `index_io.*`).
