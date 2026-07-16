## Phase 2 — Vault container ✅

**Goal:** Implement the `.osv` file format: header, index tree, chunk store, and the core vault API.

### Tasks
- [x] `src/vault/header.{h,cpp}` — parse/write the fixed-size plaintext header (magic, version, KDF block, master-key wrap, index slot A/B, active slot).
- [x] `src/vault/index.{h,cpp}` — in-memory gallery tree:
  - [x] `IndexNode` tagged node: gallery `{name, children}` or image `{name, format, w, h, orig_size, created_ts, data_span, thumb_span}` (single tagged struct rather than a union).
  - [x] Serialise / deserialise to/from a flat binary blob (versioned, hand-rolled, bounds-checked + depth-limited for zero external deps).
- [x] `src/vault/chunk_store.{h,cpp}` — append-only encrypted blob region:
  - [x] `append_chunk(plaintext) -> ChunkSpan{offset, length}` — encrypt, write nonce+ciphertext+tag, fsync.
  - [x] `read_chunk(span)` — seek, read, verify tag, return plaintext (`std::vector` or mlock'd `SecureBytes` overload).
- [x] `src/vault/vault.{h,cpp}`:
  - [x] `Vault::create(path, password, keyfile_opt, kdf_params)` — write fresh header, empty index (returns an UNLOCKED vault).
  - [x] `Vault::open(path)` — parse header; stay locked.
  - [x] `Vault::unlock(password, keyfile_opt)` — Argon2id → KEK → unwrap master key → decrypt index (falls back to the inactive slot if the active index is corrupt).
  - [x] `Vault::lock()` — wipe master key + KEK from memory.
  - [x] `Vault::add_image(gallery_path, file_data, filename)` — chunk+encrypt image; update index with crash-safe double-buffer swap. (Thumbnails wired in Phase 3.)
  - [x] `Vault::read_image(node) -> SecureBytes` — decrypt to mlock'd buffer; never touches disk.
  - [x] `Vault::remove_image(gallery_path, filename)` — drop from index (chunk orphaned, reclaimed by Phase 7 compaction).
  - [x] `Vault::list(gallery_path) -> std::vector<const IndexNode*>`.
  - [x] `Vault::create_gallery(gallery_path)` — create (nested) galleries; enforces the leaf invariant (a gallery holds sub-galleries OR images, never both).
- [x] `tests/vault/`:
  - [x] Create vault → add 3 images → lock → re-open → unlock → read each image → BLAKE2b checksum matches original.
  - [x] Crash simulation: truncate file mid-write; re-open recovers the previous valid index via the inactive slot.
  - [x] Tampered vault: flip a ciphertext byte; `read_image` returns `AuthFailed`, not garbage data.
  - [x] Integration test: gallery nesting — create nested galleries, verify `list()` returns the correct tree across a reopen.

### Acceptance criterion
All vault tests pass. A vault file created by the test can be opened, unlocked, and all images read back with matching checksums. Crash-recovery test passes.

**Status:** ✅ 55/55 tests pass (`scripts/test.sh`); `scripts/test.sh --asan` (ASAN+UBSan+LSan) is clean. The double-buffered index swap (append+fsync → write inactive slot+fsync → flip `active_slot`+fsync) survives a truncated tail by falling back to the previous slot.

> **Notes / decisions made during implementation**
> - Crypto layer gained format-driven helpers (all tested): `crypto::seal`/`open`/`open_to` (detached, explicit-nonce AEAD for the header wrap + index slots), `crypto::decrypt_chunk_to` (decrypt into a caller buffer), and `crypto::SecureBytes` (runtime-sized mlock'd buffer for decrypted images).
> - `HEADER_SIZE` fixed at 4096 bytes; the data region begins there. The header layout follows the spec table verbatim, including the 8-byte reserved gap before slot B (158–165).
> - Checksums use Monocypher's BLAKE2b (no SHA-256 in Monocypher); tests also compare full bytes.
