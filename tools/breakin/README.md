# Break-in proof tools (Phase 1)

Standalone C tools that reproduce the **core-dump break-in**: given a `.osv`
vault and its **master key** (recovered from a core dump), decrypt the vault's
index blob and an image chunk using the vault's own header + Monocypher
XChaCha20-Poly1305 — **no password involved**.

They are **not** part of the main `osv` build or CI; they are proof-of-concept
extraction tools for the break-in/hardening effort (`docs/break-in-effort.md`).

## Build

```sh
make            # builds ./breakin and ./extract_photo against the vendored Monocypher
```

Requires only a C compiler and the vendored Monocypher
(`vendor/monocypher/src`), which is already in the repo. `make clean` removes
the binaries + any output blobs.

## Reproduce the break-in

The recovered 2nd-vault master key and the full evidence are recorded in
`COLD_HANDOFF.md` (§5) and `docs/break-in-effort.md` (§5). Grab the key hex
(`<key_hex>`, 64 hex chars) and the vault path, then:

```sh
make
# 1) Decrypt the active index slot (Poly1305 TAG VERIFIED) -> index_slot0.bin
./breakin <vault> <key_hex> ./index_slot

# 2) Walk that index, find the first image, decrypt its data chunk -> photo_decrypted.bin
./extract_photo <vault> <key_hex> ./index_slot0.bin ./photo_decrypted.bin
```

Expected output (from the 2026-08-25 2nd-vault):

```
flags=0x00000000 active_slot=0   (legacy, password-only, no framing)
slot[0]: crypto_aead_unlock -> TAG VERIFIED (success) (plaintext 35139 bytes)
FIRST IMAGE: ... 3955x2225  data_length=8741210
image chunk: crypto_aead_unlock -> TAG VERIFIED (success)
magic: ff d8 ff e0   (JPEG)
md5: 6b97bcf4ae64020f745bd9c1c822e78c
```

## What it proves

- The **master key** (a 32-byte XChaCha20 key) fully decrypts the vault — the
  index (node tree) and every image chunk. Recovering that one key from a core
  dump makes the *entire* vault readable offline.
- The leak is **key material in memory reaching disk** (the core dump), not a
  weakness in the AEAD or KDF. That is the vector the mlock / hardening phases
  target.

## Notes

- `breakin` tries the active index slot first, then the other (the header holds
  both slots' offset/length/nonce).
- `extract_photo` walks the decrypted index (`IndexNode` tree) to find the first
  image node's `data_offset`/`data_length`, then reads + decrypts that chunk
  (`nonce[24] | cipher | tag[16]`).
- These are deliberately minimal (a few hundred lines) so the proof is
  auditable; they are not general-purpose vault tooling.
