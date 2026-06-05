#pragma once

// XChaCha20-Poly1305 AEAD chunk encryption (via Monocypher).
//
// On-disk chunk layout (matches CLAUDE.md / the .osv data region):
//
//     nonce[24] | ciphertext[plaintext_len] | tag[16]
//
// A fresh random 24-byte nonce is generated per encrypt call (invariant #3); the
// 192-bit nonce space makes random nonces safe with no counter state to persist.
// The Poly1305 tag is always verified before any plaintext is returned
// (invariant #4).

#include <cstdint>
#include <span>
#include <vector>

#include "crypto.h"   // KEY_SIZE, NONCE_SIZE, TAG_SIZE

namespace crypto {

// Encrypt `plaintext` under `key`. Writes `nonce|ciphertext|tag` into `out`
// (resized to plaintext.size() + NONCE_SIZE + TAG_SIZE). `ad` is optional
// associated data, authenticated but not encrypted. Returns false (logged) only
// if the CSPRNG fails — in which case `out` is left empty.
[[nodiscard]] bool encrypt_chunk(std::span<const uint8_t, KEY_SIZE> key,
                                 std::span<const uint8_t>           plaintext,
                                 std::vector<uint8_t>&              out,
                                 std::span<const uint8_t>           ad = {}) noexcept;

// Decrypt a `nonce|ciphertext|tag` chunk under `key`, verifying the tag first.
// On success writes the plaintext into `out_plaintext` and returns true. On any
// failure (short chunk or authentication failure) returns false and leaves
// `out_plaintext` empty — never exposes unauthenticated bytes.
[[nodiscard]] bool decrypt_chunk(std::span<const uint8_t, KEY_SIZE> key,
                                 std::span<const uint8_t>           chunk,
                                 std::vector<uint8_t>&              out_plaintext,
                                 std::span<const uint8_t>           ad = {}) noexcept;

} // namespace crypto
