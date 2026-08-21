#include "kdf.h"

#include <cstring>
#include <limits>

#include <monocypher.h>

#include "platform/safe_print.h"

namespace crypto {

bool derive_key(std::span<const uint8_t>            password,
                std::span<const uint8_t>            keyfile,
                std::span<const uint8_t, SALT_SIZE> salt,
                const KdfParams&                    params,
                SecureBuffer<KEY_SIZE>&             out_key,
                KdfInputFormat                      format)
{
    // Argon2 needs a caller-allocated work area of 1024 * nb_blocks bytes.
    // Monocypher requires at least 8 blocks per lane; Header::parse enforces
    // the same policy on untrusted vaults, this guards direct callers.
    if (params.t_cost == 0 || params.t_cost > MAX_KDF_T_COST ||
        params.parallelism == 0 || params.parallelism > MAX_KDF_PARALLELISM ||
        params.m_cost_kib < 8 * params.parallelism || params.m_cost_kib > MAX_KDF_M_COST_KIB) {
        platform::safe_println(stderr, "[crypto] rejected invalid Argon2 parameters");
        return false;
    }

    constexpr std::array<uint8_t, 16> domain = {
        'O','S','V','-','K','D','F','-','I','N','P','U','T','-','2',0};
    const size_t prefix_size = format == KdfInputFormat::DomainSeparatedV2
        ? domain.size() + sizeof(uint64_t) * 2 : 0;
    if (constexpr size_t max_argon_input = std::numeric_limits<uint32_t>::max();
        password.size() > max_argon_input || keyfile.size() > max_argon_input ||
        prefix_size > max_argon_input - password.size() ||
        keyfile.size() > max_argon_input - prefix_size - password.size()) {
        return false;
    }

    // Build the legacy concatenation or the unambiguous v2 encoding in locked,
    // automatically wiped memory.
    SecureBytes secret;
    if (!secret.resize(prefix_size + password.size() + keyfile.size())) return false;
    size_t offset = 0;
    if (format == KdfInputFormat::DomainSeparatedV2) {
        std::memcpy(secret.data(), domain.data(), domain.size());
        offset = domain.size();
        const auto put_u64_le = [&](uint64_t value) {
            for (unsigned i = 0; i < 8; ++i)
                secret.data()[offset++] = static_cast<uint8_t>(value >> (i * 8));
        };
        put_u64_le(password.size());
        put_u64_le(keyfile.size());
    }
    if (!password.empty())
        std::memcpy(secret.data() + offset, password.data(), password.size());
    offset += password.size();
    if (!keyfile.empty())
        std::memcpy(secret.data() + offset, keyfile.data(), keyfile.size());

    const size_t work_size = static_cast<size_t>(params.m_cost_kib) * 1024u;
    SecureBytes work_area;
    if (!work_area.resize(work_size)) return false;

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

    work_area.wipe();  // holds password-derived state; destructor wipes again
    return true;
}

} // namespace crypto
