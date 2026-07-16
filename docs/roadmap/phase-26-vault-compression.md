## Phase 26 — Transparent vault compression (adaptive, store-if-smaller) ✅

**Goal:** Compress each chunk's plaintext **before encryption**, adaptively —
keep the compressed form only when it is meaningfully smaller, otherwise store
raw — so compressible payloads (BMP/TGA/HDR images, index blobs) shrink the
vault while already-compressed media (JPEG/PNG/WebP/HEIC/AVIF, video) stores raw
with **zero size regression**. Fully transparent to every caller; invariant #1
(no plaintext to disk) holds throughout.

> **Evaluated 2026-07-03:** the vault does not compress today —
> `ChunkStore::append_chunk` encrypts the raw plaintext verbatim; the only
> compression code in the tree is miniz *decompression* for ZIP/CBZ import.

### Tasks
- [x] **Chunk plaintext framing** — prefix the chunk *plaintext* (inside the AEAD, so the method flag is authenticated) with `method u8` (0 = raw, 1 = deflate) and, for compressed chunks, an `orig_len` field. Bound `orig_len` against the max chunk plaintext size **before allocating** (decompression-bomb guard — the Phase 7 OOM lesson).
- [x] **Vault flag / back-compat** — a bit in the header's reserved `flags` u32 marks "chunks are method-prefixed". Existing vaults keep the bit clear and are read **and appended** in legacy raw mode forever (no migration, no mixed chunk interpretation within one vault). New vaults set the bit.
- [x] **Compressor** — miniz `tdefl`/`tinfl` (already vendored + premake-compiled with `MINIZ_NO_ZLIB_COMPATIBLE_NAMES`); **no new dependency**. Compress in `append_chunk`; keep the compressed form only when ≤ ~95% of the original, else store raw.
- [x] **Decompress path** — decrypt into an mlock'd `SecureBytes` → inflate into a second mlock'd buffer sized by the validated `orig_len` → wipe the intermediate. No disk, ever.
- [x] **Index blobs framing** — sealed via `crypto::seal` + `append_raw`, then framed explicitly at `commit_index`/`unlock`/`compact` sites; the method byte is inside the sealed blob, so compressibility is authenticated. Gallery names/tags on large vaults benefit from compression without extra code.
- [x] **Document the side channel** — ciphertext length now reveals plaintext *compressibility*; record it as accepted for the local-attacker threat model (CLAUDE.md hardening notes).
- [x] Update `CLAUDE.md` (chunk layout, deferred-decisions table) + the container-format reference section below + `mem:core`.
- [x] `tests/` — round-trips (compressed + raw) with checksums; a BMP/TGA vault is measurably smaller while a JPEG vault stores raw (no growth); a legacy (flag-clear) vault fixture opens, reads, and appends unchanged; hostile `method`/`orig_len` fuzz corpus; export/transfer/compact still checksum-match on compressed vaults; ASAN clean.

**Out of scope (YAGNI):** migrating/recompressing legacy vaults (even via compaction); zstd/LZ4; per-file or user-facing compression settings; compressing the plaintext header.

### Acceptance criterion
A vault of BMP/TGA fixtures is measurably smaller than the summed plaintext; a
vault of JPEGs is no larger than today (raw fallback); a legacy vault opens,
reads, and appends unchanged; export/transfer checksums match on compressed
vaults; the extended fuzz suite and ASAN pass.

**Status:** ✅ 660/660 tests pass; `scripts/test.sh --asan` clean. Pure adaptive store-if-smaller deflate framing (miniz `tdefl`/`tinfl`) wraps chunk plaintext before encryption; method byte + bounded orig_len live inside the AEAD for authentication. New vaults set the framing flag; legacy vaults read and append raw forever. Index blobs are framed at three sites (`commit_index`, `unlock`, `compact`), and the compact-index-blob write path revealed a real bug (now fixed). Ciphertext length reveals plaintext compressibility — accepted for the local-attacker threat model.
