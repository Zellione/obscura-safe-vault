#pragma once

// Platform CSPRNG shim. Monocypher ships no RNG, so this fills the gap with the
// OS entropy source. Used for nonces, salts, and master-key generation.

#include <cstdint>
#include <span>

namespace crypto {

// Fill the whole span with cryptographically secure random bytes.
// Returns false (and logs to stderr) if the OS RNG cannot be read; callers
// MUST check the result and never proceed with a half-filled buffer.
[[nodiscard]] bool fill_random(std::span<uint8_t> out) noexcept;

} // namespace crypto
