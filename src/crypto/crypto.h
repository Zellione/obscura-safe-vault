#pragma once

// Phase 1 stub: Monocypher wrappers for XChaCha20-Poly1305 + Argon2id.
// Full implementation in Phase 1.

#include <cstddef>
#include <cstdint>
#include <span>

namespace crypto {

// Key / nonce / tag sizes for XChaCha20-Poly1305 (all in bytes)
inline constexpr size_t KEY_SIZE   = 32;
inline constexpr size_t NONCE_SIZE = 24;  // XChaCha20 extended nonce
inline constexpr size_t TAG_SIZE   = 16;
inline constexpr size_t SALT_SIZE  = 16;

// TODO (Phase 1): SecureBuffer — mlock'd, crypto_wipe'd on destruction
// TODO (Phase 1): random_bytes() — getrandom / getentropy / BCryptGenRandom
// TODO (Phase 1): derive_key()  — Argon2id via monocypher
// TODO (Phase 1): encrypt_chunk() / decrypt_chunk() — XChaCha20-Poly1305

} // namespace crypto
