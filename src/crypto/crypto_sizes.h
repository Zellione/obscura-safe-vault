#pragma once

// Sizes for XChaCha20-Poly1305 + Argon2id, in bytes.
// Extracted into a separate header so all crypto sub-modules can include this
// without causing include-order violations (S954 requires includes before code).

#include <cstddef>

namespace crypto {

inline constexpr size_t KEY_SIZE   = 32;
inline constexpr size_t NONCE_SIZE = 24;  // XChaCha20 extended nonce
inline constexpr size_t TAG_SIZE   = 16;  // Poly1305 authentication tag
inline constexpr size_t SALT_SIZE  = 16;
// Stable per-node identity (Phase 99 / OSV-AUD-004): a random 128-bit id bound
// into every chunk's associated data, and a per-record id bound to each chunk
// so a replayed old (dead) record of the same node/role fails authentication.
inline constexpr size_t NODE_ID_SIZE = 16;

} // namespace crypto
