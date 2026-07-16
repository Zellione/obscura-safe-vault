## Phase 1 — Crypto core ✅

**Goal:** Implement and test the full cryptographic primitive layer.

### Tasks
- [x] `src/crypto/random.{h,cpp}` — platform CSPRNG shim: `getrandom` (Linux), `getentropy` (macOS), `BCryptGenRandom` (Windows). Exposed as `crypto::fill_random(std::span<uint8_t>)`.
- [x] `src/crypto/secure_mem.h` — `SecureBuffer<N>`: buffer with `mlock` on construction and `crypto_wipe` + `munlock` on destruction (header-only template; mlock failure is logged, not fatal).
- [x] `src/crypto/kdf.{h,cpp}` — `crypto::derive_key(password, keyfile_opt, salt, params, out_key)` wrapping `crypto_argon2` with `CRYPTO_ARGON2_ID`.
- [x] `src/crypto/aead.{h,cpp}` — `encrypt_chunk` / `decrypt_chunk` wrapping `crypto_aead_lock` / `crypto_aead_unlock` (XChaCha20-Poly1305). Generates fresh random nonce per encrypt call.
- [x] `tests/crypto/` — unit tests (run via `scripts/test.sh`):
  - [x] Known-answer tests for Argon2id (RFC 9106 §5.3 vector).
  - [x] Known-answer tests for XChaCha20-Poly1305 (draft-irtf-cfrg-xchacha-03 A.3.1 vector).
  - [x] Round-trip: encrypt → decrypt → compare plaintext (incl. empty + 1 MiB).
  - [x] Tamper detection: flip a byte in nonce / ciphertext / tag → `decrypt_chunk` fails.
  - [x] `SecureBuffer` wipe verification (storage zeroed after destruction; ASAN-clean).

### Acceptance criterion
All crypto unit tests pass. `valgrind --leak-check=full` (or ASAN + LSAN) reports no leaks. `mlock` failures are handled gracefully (logged, not fatal) on systems with low `RLIMIT_MEMLOCK`.

**Status:** ✅ 12/12 tests pass; `scripts/test.sh --asan` (ASAN+UBSan+LSan) is clean; `SecureBuffer` degrades gracefully when `mlock` is unavailable.
