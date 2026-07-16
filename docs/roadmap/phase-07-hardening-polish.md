## Phase 7 — Hardening & polish ✅

**Goal:** Close security gaps, handle edge cases, and add deletion + compaction.

### Tasks
- [x] **Crash-safe index swap** — verify double-buffer logic with injected write failures.
- [x] **Compaction** — `Vault::compact()`: copy live chunks to a new file, rewrite header and index; rename atomically. Run on request or when waste exceeds a threshold.
- [x] **Delete image** — `Vault::remove_image` + trigger compaction.
- [x] **Password change** — re-wrap master key with new Argon2id-derived KEK; no re-encryption of data chunks.
- [x] **Passphrase strength meter** — on vault creation; classify weak/medium/strong; offer random diceware passphrase generation.
- [x] **Keyfile flow** — full UX: create keyfile, select on unlock, clear error on wrong keyfile.
- [x] **Secure-memory audit** — review all code paths: no key material on the stack without wipe, no decrypted pixel data outside mlock'd buffers.
- [x] **Fuzz testing** — feed malformed `.osv` files to `Vault::open` and `decode_from_memory`; must not crash.
- [x] `tests/vault/` — add compaction, delete, and password-change tests.

### Acceptance criterion
Fuzz test runs 10,000 malformed inputs without crashing. Delete + compact cycle reduces file size. Password change succeeds without re-encrypting data chunks.

**Status:** ✅ 146/146 tests pass under `scripts/test.sh` and ASAN+UBSan+LSan. The seeded
fuzz suite covers 10,000+ malformed inputs (4,000 hostile `.osv` files against
`Vault::open`/`unlock`, 3,000 index blobs against `deserialize_index`, 3,000 image buffers
against `decode_from_memory`) and runs as part of the normal test suite. Compaction tests
verify file shrinkage, content checksums, thumbnail carry-over, and cold reopen; password
change is verified to leave data-chunk bytes on disk untouched.

> **Notes / decisions made during implementation**
> - **Injected write failures.** `fileutil::sync()` gained a tiny arm-once fault-injection
>   counter (`inject_sync_failure`) — there is no portable way to make a real fsync fail on
>   demand. Tests fail the commit at *every* sync step and verify the vault always reopens
>   with a valid index (the previous one, or the new one if the flip was already written).
>   On-disk corruption tests additionally cover torn index blobs and the
>   crash-between-step-B-and-C header state.
> - **Compaction copies ciphertext verbatim.** Live chunks are byte-identical copies
>   (`nonce|ciphertext|tag`) — same key, same plaintext, so no information leak and no
>   pointless decrypt/re-encrypt pass. The new file is fully written + fsync'd, then
>   atomically renamed over the original (plus best-effort parent-dir fsync), then the
>   vault's handle is reopened. `wasted_bytes()` reports reclaimable space;
>   `remove_image` auto-compacts when waste ≥ 256 KiB *and* ≥ ¼ of the file.
> - **Fuzzing found a real OOM-DoS:** chunk/index lengths from corruptible metadata were
>   used to size buffer allocations *before* any bounds check (`ChunkStore::read_chunk`/
>   `read_raw`), so a hostile length meant a `bad_alloc` abort. Reads are now bounds-checked
>   against the file size before allocating. Relatedly, `Header::parse` now rejects hostile
>   KDF parameters (the Argon2 work area is attacker-sized otherwise; bounds: t_cost ≤ 512,
>   m_cost ≤ 4 GiB, lanes ≤ 64, nb_blocks ≥ 8·lanes) and out-of-range `active_slot`, and
>   `derive_key` survives work-area allocation failure instead of throwing.
> - **Passphrase module** (`ui/passphrase.{h,cpp}`): char-class entropy estimate
>   (Weak < 50 bits ≤ Medium < 80 ≤ Strong) + generation from an embedded 256-word list —
>   one CSPRNG byte per word (zero modulo bias), 8 words ≈ 64 bits by default. Generated
>   bytes go straight into the mlock'd `SecureTextField`; the unlock screen renders the
>   revealed phrase via `string_view` over that buffer (no unlocked copy).
> - **Keyfile creation** (`platform::write_new_keyfile`): 64 CSPRNG bytes via
>   `SDL_ShowSaveFileDialog`; refuses to overwrite an existing file (clobbering a keyfile
>   would permanently lock every vault bound to it).
> - **Secure-memory audit fixes:** `platform::read_file` (carries keyfiles) now sizes the
>   buffer once instead of chunk-growing — reallocation used to strew stale key-material
>   copies across freed heap blocks, and a 64 KiB stack scratch went unwiped;
>   `derive_key`'s password‖keyfile scratch moved from a wiped-but-swappable heap vector
>   into mlock'd `SecureBytes`. *Accepted deviations:* decoded RGB pixels (stb_image
>   output) live in regular heap `ImageData` — mlocking them needs a custom `STBI_MALLOC`
>   allocator (revisit if warranted); index blobs (filenames/metadata) are plain vectors —
>   metadata, not key material; the keyfile passes through one wiped (non-mlock'd) vector
>   in the unlock flow.
> - **Known limitation (documented, not fixed):** the header's master-key wrap is a single
>   copy — a torn 4 KiB header write during `change_password` could destroy the wrap. A
>   crash-safe wrap swap would need a format change (double-buffered wrap), deferred.
