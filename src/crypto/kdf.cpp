#include "kdf.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

#include <monocypher.h>

namespace crypto {

bool derive_key(std::span<const uint8_t>            password,
                std::span<const uint8_t>            keyfile,
                std::span<const uint8_t, SALT_SIZE> salt,
                const KdfParams&                    params,
                SecureBuffer<KEY_SIZE>&             out_key) noexcept
{
    // Concatenate password ‖ keyfile into a scratch buffer that we wipe before
    // returning. (mlock here would need a runtime-sized SecureBuffer; the buffer
    // is short-lived and explicitly wiped, satisfying invariant #2.)
    std::vector<uint8_t> secret;
    secret.reserve(password.size() + keyfile.size());
    secret.insert(secret.end(), password.begin(), password.end());
    secret.insert(secret.end(), keyfile.begin(),  keyfile.end());

    // Argon2 needs a caller-allocated work area of 1024 * nb_blocks bytes.
    const size_t work_size = static_cast<size_t>(params.m_cost_kib) * 1024u;
    void* work_area = std::malloc(work_size);
    if (!work_area) {
        std::fprintf(stderr,
            "[crypto::kdf] failed to allocate %zu-byte Argon2 work area\n", work_size);
        crypto_wipe(secret.data(), secret.size());
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

    crypto_argon2(out_key.data(), static_cast<uint32_t>(out_key.size()),
                  work_area, config, inputs, crypto_argon2_no_extras);

    // Wipe and free everything that touched the secret.
    crypto_wipe(work_area, work_size);
    std::free(work_area);
    crypto_wipe(secret.data(), secret.size());
    return true;
}

} // namespace crypto
