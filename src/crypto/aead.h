#pragma once

// XChaCha20-Poly1305 AEAD chunk encryption (via Monocypher).
//
// On-disk chunk layout (matches AGENTS.md / the .osv data region):
//
//     nonce[24] | ciphertext[plaintext_len] | tag[16]
//
// A fresh random 24-byte nonce is generated per encrypt call (invariant #3); the
// 192-bit nonce space makes random nonces safe with no counter state to persist.
// The Poly1305 tag is always verified before any plaintext is returned
// (invariant #4).

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "crypto_sizes.h"   // KEY_SIZE, NONCE_SIZE, TAG_SIZE, NODE_ID_SIZE

namespace crypto {

// --- Context-bound AEAD associated data (Phase 99 / OSV-AUD-004) ------------
//
// Every encrypted record's authentication is bound to a logical identity so a
// complete-record swap/splice/replay fails verification instead of silently
// substituting content under the same master key. `build_chunk_ad` turns a
// ChunkTag into the exact 38-byte canonical AD (byte-for-byte deterministic,
// so Linux and Windows verify identically):
//
//   byte   0    : domain        (u8)
//   byte   1    : version       (u8, CHUNK_AD_VERSION)
//   bytes  2..17: owner         (the node's node_id, or the vault's header salt
//                                for Index / MkWrap records)
//   bytes 18..33: record        (a fresh random per-record id, stored in the
//                                authenticated index; zero for Index / MkWrap)
//   bytes 34..37: sequence      (u32 LE; nonzero only for VIDEO chunks)
//
// All lengths are fixed-width little-endian, so no length prefixes are needed
// and the encoding cannot drift between platforms.

enum class ChunkDomain : uint8_t {
    Data   = 0,  // image main data chunk
    Thumb  = 1,  // stored image thumbnail
    Poster = 2,  // video poster frame
    Video  = 3,  // one video data chunk (sequence-numbered)
    Index  = 4,  // sealed index blob                          (owner = vault salt)
    MkWrap = 5,  // master-key wrap in the header              (owner = vault salt)
};

inline constexpr uint8_t CHUNK_AD_VERSION = 1;

inline constexpr size_t AD_SIZE = 1 + 1 + NODE_ID_SIZE + NODE_ID_SIZE + 4;  // 38

// The logical identity of one chunk record. `context_bound == false` means a
// LEGACY record encrypted with NO associated data (unchanged pre-Phase-99
// behavior) — carried on media nodes during the v1→v2 migration window.
struct ChunkTag {
    ChunkDomain domain = ChunkDomain::Data;
    std::array<uint8_t, NODE_ID_SIZE> owner{};    // node_id, or header salt for Index/MkWrap
    std::array<uint8_t, NODE_ID_SIZE> record{};   // per-record random id (zeros for Index/MkWrap)
    uint32_t   sequence      = 0;
    bool       context_bound = false;
};

// Build the canonical 38-byte AD for `t`. Always deterministic; the caller
// passes the result (or an empty span when `context_bound` is false) to the
// encrypt/seal/open functions as `ad`.
[[nodiscard]] std::array<uint8_t, AD_SIZE> build_chunk_ad(const ChunkTag& t) noexcept;

// Encrypt `plaintext` under `key`. Writes `nonce|ciphertext|tag` into `out`
// (resized to plaintext.size() + NONCE_SIZE + TAG_SIZE). `ad` is optional
// associated data, authenticated but not encrypted. Returns false (logged) if
// the CSPRNG or output allocation fails — in which case `out` is left empty.
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
// failure (short chunk, authentication failure, or output allocation failure)
// returns false and leaves
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
// (resized to plaintext.size() + TAG_SIZE). Returns false if output allocation
// fails; `out` is then left empty.
[[nodiscard]] bool seal(std::span<const uint8_t, KEY_SIZE>   key,
                        std::span<const uint8_t, NONCE_SIZE> nonce,
                        std::span<const uint8_t>             plaintext,
                        std::vector<uint8_t>&                out,
                        std::span<const uint8_t>             ad = {}) noexcept;

// Open a `ciphertext|tag` blob under `key`+`nonce`, verifying the tag first. On
// success writes plaintext into `out_plaintext` and returns true. On any failure
// (short input, authentication failure, or output allocation failure) returns
// false and leaves the output
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
