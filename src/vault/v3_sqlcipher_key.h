#pragma once

#include "crypto/crypto_sizes.h"
#include "crypto/secure_mem.h"

#include <cstdint>
#include <span>

namespace vault::v3 {

// SQLCipher recognizes a direct 256-bit key only in its x'<64 hex>' keyspec
// form. Keep that transient representation in locked, wipe-on-release memory.
inline constexpr size_t SQLCIPHER_RAW_KEYSPEC_SIZE = 67;

[[nodiscard]] crypto::SecureBuffer<SQLCIPHER_RAW_KEYSPEC_SIZE>
sqlcipher_raw_keyspec(std::span<const uint8_t, crypto::KEY_SIZE> key) noexcept;

} // namespace vault::v3
