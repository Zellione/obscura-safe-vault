#pragma once

// Argon2id password-based key derivation (via Monocypher's crypto_argon2).
//
// Derives the Key-Encryption-Key (KEK) from the user's password and an optional
// keyfile:  KEK = Argon2id(password ‖ keyfile_bytes, salt).  The KEK unwraps the
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

// Derive a 32-byte key from password (+ optional keyfile) and salt.
// `keyfile` may be empty. Returns false (logged) if the Argon2 work area cannot
// be allocated. The password/keyfile bytes are concatenated into a wiped
// scratch buffer; no plaintext secret is left in memory on return.
[[nodiscard]] bool derive_key(std::span<const uint8_t>            password,
                              std::span<const uint8_t>            keyfile,
                              std::span<const uint8_t, SALT_SIZE> salt,
                              const KdfParams&                    params,
                              SecureBuffer<KEY_SIZE>&             out_key);

} // namespace crypto
