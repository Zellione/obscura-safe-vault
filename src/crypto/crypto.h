#pragma once

// Umbrella header for the crypto module + the shared primitive sizes.
//
// The size constants are defined here (before the sub-includes) so every crypto
// header can rely on them via a single `#include "crypto.h"` without redefining.
// Callers who want the whole layer can also just include this one file.

#include <cstddef>

namespace crypto {

// Sizes for XChaCha20-Poly1305 + Argon2id, in bytes.
inline constexpr size_t KEY_SIZE   = 32;
inline constexpr size_t NONCE_SIZE = 24;  // XChaCha20 extended nonce
inline constexpr size_t TAG_SIZE   = 16;  // Poly1305 authentication tag
inline constexpr size_t SALT_SIZE  = 16;

} // namespace crypto

// Sub-modules (each includes this header back for the constants; #pragma once
// makes the re-entry a no-op since the constants above are already defined).
#include "secure_mem.h"
#include "random.h"
#include "kdf.h"
#include "aead.h"
