#include "kdf.h"

#include <cstring>
#include <print>
#include <vector>

#include <monocypher.h>

namespace crypto {

bool derive_key(std::span<const uint8_t>            password,
                std::span<const uint8_t>            keyfile,
                std::span<const uint8_t, SALT_SIZE> salt,
                const KdfParams&                    params,
                SecureBuffer<KEY_SIZE>&             out_key)
{
    // Argon2 needs a caller-allocated work area of 1024 * nb_blocks bytes.
    // Monocypher requires at least 8 blocks per lane; Header::parse enforces
    // the same policy on untrusted vaults, this guards direct callers.
    if (params.t_cost == 0 || params.parallelism == 0 ||
        params.m_cost_kib < 8 * params.parallelism) {
        std::println(stderr, "[crypto] rejected invalid Argon2 parameters");
        return false;
    }

    // Concatenate password ‖ keyfile into an mlock'd scratch buffer (wiped on
    // every exit path by SecureBytes' destructor).
    SecureBytes secret;
    if (!secret.resize(password.size() + keyfile.size())) return false;
    if (!password.empty())
        std::memcpy(secret.data(), password.data(), password.size());
    if (!keyfile.empty())
        std::memcpy(secret.data() + password.size(), keyfile.data(), keyfile.size());

    const size_t work_size = static_cast<size_t>(params.m_cost_kib) * 1024u;
    std::vector<uint8_t> work_area;
    try {
        work_area.resize(work_size);
    } catch (const std::bad_alloc&) {
        std::println(stderr, "[crypto] Argon2 work area of {} KiB unavailable",
                     params.m_cost_kib);
        return false;
    }

    const crypto_argon2_config config{
        .algorithm = CRYPTO_ARGON2_ID,
        .nb_blocks = params.m_cost_kib,
        .nb_passes = params.t_cost,
        .nb_lanes  = params.parallelism,
    };
    const crypto_argon2_inputs inputs{
        .pass      = secret.data(),
        .salt      = salt.data(),
        .pass_size = static_cast<uint32_t>(secret.size()),
        .salt_size = static_cast<uint32_t>(salt.size()),
    };

    crypto_argon2(out_key.data(), static_cast<uint32_t>(crypto::KEY_SIZE),
                  work_area.data(), config, inputs, crypto_argon2_no_extras);

    crypto_wipe(work_area.data(), work_size);  // holds password-derived state
    return true;
}

} // namespace crypto
