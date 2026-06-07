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

#include "crypto_sizes.h"   // KEY_SIZE, NONCE_SIZE, TAG_SIZE

namespace crypto {

// Encrypt `plaintext` under `key`. Writes `nonce|ciphertext|tag` into `out`
// (resized to plaintext.size() + NONCE_SIZE + TAG_SIZE). `ad` is optional
// associated data, authenticated but not encrypted. Returns false (logged) only
// if the CSPRNG fails — in which case `out` is left empty.
[[nodiscard]] bool encrypt_chunk(std::span<const uint8_t, KEY_SIZE> key,
                                 std::span<const uint8_t>           plaintext,
                                 std::vector<uint8_t>&              out,
                                 std::span<const uint8_t>           ad = {}) noexcept;

// Plaintext length carried by a `nonce|ciphertext|tag` chunk of `chunk_size`
// bytes. Returns 0 for any chunk too small to hold a nonce + tag.
[[nodiscard]] constexpr size_t chunk_plaintext_len(size_t chunk_size) noexcept
{
    return chunk_size < NONCE_SIZE + TAG_SIZE ? 0 : chunk_size - NONCE_SIZE - TAG_SIZE;
}

// Decrypt a `nonce|ciphertext|tag` chunk under `key`, verifying the tag first.
// On success writes the plaintext into `out_plaintext` and returns true. On any
// failure (short chunk or authentication failure) returns false and leaves
// `out_plaintext` empty — never exposes unauthenticated bytes.
[[nodiscard]] bool decrypt_chunk(std::span<const uint8_t, KEY_SIZE> key,
                                 std::span<const uint8_t>           chunk,
                                 std::vector<uint8_t>&              out_plaintext,
                                 std::span<const uint8_t>           ad = {}) noexcept;

// Same as decrypt_chunk but writes into a caller-provided buffer that must be
// exactly chunk_plaintext_len(chunk.size()) bytes. Lets callers decrypt straight
// into mlock'd memory (SecureBytes) so decrypted image data never passes through
// an unlocked heap buffer (invariant #1). Returns false (wiping `out`) on short
// input, size mismatch, or authentication failure.
[[nodiscard]] bool decrypt_chunk_to(std::span<const uint8_t, KEY_SIZE> key,
                                    std::span<const uint8_t>           chunk,
                                    std::span<uint8_t>                 out,
                                    std::span<const uint8_t>           ad = {}) noexcept;

// --- Detached form (explicit nonce, no nonce prefix) ----------------------
//
// Unlike encrypt_chunk/decrypt_chunk, these take a caller-supplied nonce and the
// on-disk blob is just `ciphertext|tag` — the nonce lives elsewhere. The vault
// uses this for the fixed-offset header master-key wrap and the double-buffered
// index slots, where the nonce is stored in the header (flipping `active_slot`
// atomically commits both the new index location and its nonce).
//
// The caller is responsible for nonce uniqueness per (key, message). The vault
// generates a fresh random 24-byte nonce on every write (invariant #3).

// Seal `plaintext` under `key`+`nonce`. Writes `ciphertext|tag` into `out`
// (resized to plaintext.size() + TAG_SIZE). Cannot fail (no RNG, no auth).
void seal(std::span<const uint8_t, KEY_SIZE>   key,
          std::span<const uint8_t, NONCE_SIZE> nonce,
          std::span<const uint8_t>             plaintext,
          std::vector<uint8_t>&                out,
          std::span<const uint8_t>             ad = {}) noexcept;

// Open a `ciphertext|tag` blob under `key`+`nonce`, verifying the tag first. On
// success writes plaintext into `out_plaintext` and returns true. On any failure
// (short input or authentication failure) returns false and leaves the output
// empty — never exposes unauthenticated bytes (invariant #4).
[[nodiscard]] bool open(std::span<const uint8_t, KEY_SIZE>   key,
                        std::span<const uint8_t, NONCE_SIZE> nonce,
                        std::span<const uint8_t>             sealed,
                        std::vector<uint8_t>&                out_plaintext,
                        std::span<const uint8_t>             ad = {}) noexcept;

// Like open() but writes into a caller-provided buffer that must be exactly
// sealed.size() - TAG_SIZE bytes. Lets the vault unwrap the master key directly
// into mlock'd memory (SecureBuffer) — the master key never touches an unlocked
// heap buffer. Returns false (wiping `out`) on short input, size mismatch, or
// authentication failure.
[[nodiscard]] bool open_to(std::span<const uint8_t, KEY_SIZE>   key,
                           std::span<const uint8_t, NONCE_SIZE> nonce,
                           std::span<const uint8_t>             sealed,
                           std::span<uint8_t>                   out,
                           std::span<const uint8_t>             ad = {}) noexcept;

} // namespace crypto
