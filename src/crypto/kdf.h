#pragma once

// Argon2id password-based key derivation (via Monocypher's crypto_argon2).
//
// Derives the Key-Encryption-Key (KEK) from the user's password and an optional
// keyfile. New vaults encode typed, length-prefixed fields before Argon2;
// LegacyConcat remains available solely for opening existing vaults.
// vault's random 32-byte master key (master-key wrap lives in the vault layer).

#include <cstdint>
#include <span>

#include "crypto_sizes.h"   // KEY_SIZE, SALT_SIZE
#include "secure_mem.h"

namespace crypto {

// Argon2id cost parameters. Persisted in the vault header so a vault can always
// be re-derived with the parameters it was created with.
struct KdfParams {
    uint32_t t_cost;       // passes (nb_passes)
    uint32_t m_cost_kib;   // memory in KiB == nb_blocks (1 block = 1 KiB)
    uint32_t parallelism;  // lanes (nb_lanes)
};

// 64 MiB / 3 passes — RFC 9106's second recommended option. Final calibration
// to the target machine is a Phase 2/7 concern; this is a sane default.
inline constexpr KdfParams DEFAULT_KDF_PARAMS{
    .t_cost = 3, .m_cost_kib = 65536, .parallelism = 1};

inline constexpr uint32_t MAX_KDF_T_COST = 10;
inline constexpr uint32_t MAX_KDF_M_COST_KIB = 256u * 1024;
inline constexpr uint32_t MAX_KDF_PARALLELISM = 16;

enum class KdfInputFormat : uint8_t {
    LegacyConcat,
    DomainSeparatedV2,
};

// Derive a 32-byte key from password (+ optional keyfile) and salt.
// `keyfile` may be empty. Returns false if parameters/input sizes are invalid
// or secure allocation fails. Input and workspace live in best-effort locked,
// wiping buffers; no plaintext secret or password-derived state remains.
[[nodiscard]] bool derive_key(std::span<const uint8_t>            password,
                              std::span<const uint8_t>            keyfile,
                              std::span<const uint8_t, SALT_SIZE> salt,
                              const KdfParams&                    params,
                              SecureBuffer<KEY_SIZE>&             out_key,
                              KdfInputFormat format = KdfInputFormat::DomainSeparatedV2);

} // namespace crypto
