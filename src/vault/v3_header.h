#pragma once

#include "crypto/kdf.h"
#include "vault/v3_crypto_spec.h"

#include <array>
#include <cstdint>
#include <span>

namespace vault::v3 {

inline constexpr size_t V3_HEADER_SIZE = 256;
inline constexpr size_t V3_MASTER_WRAP_AD_SIZE = 26;

enum class HeaderStatus : uint8_t {
    Ok,
    Invalid,
    UnsupportedVersion,
    AuthenticationFailed,
    CryptoError
};

struct V3Header {
    uint32_t flags = 0;
    crypto::KdfParams kdf{};
    std::array<uint8_t, crypto::SALT_SIZE> salt{};
    bool keyfile_required = false;
    std::array<uint8_t, crypto::NONCE_SIZE> nonce{};
    std::array<uint8_t, crypto::KEY_SIZE> wrapped_master_key{};
    std::array<uint8_t, crypto::TAG_SIZE> tag{};
    Id vault_id{};
};

[[nodiscard]] HeaderStatus parse_v3_header(std::span<const uint8_t, V3_HEADER_SIZE> raw,
                                           V3Header& out) noexcept;
[[nodiscard]] std::array<uint8_t, V3_HEADER_SIZE>
serialize_v3_header(const V3Header& header) noexcept;
[[nodiscard]] HeaderStatus
create_v3_header(std::span<const uint8_t> password, std::span<const uint8_t> keyfile,
                 const crypto::KdfParams& kdf, V3Header& header,
                 crypto::SecureBuffer<crypto::KEY_SIZE>& master_key) noexcept;
[[nodiscard]] HeaderStatus rewrap_v3_header(const V3Header& current,
                                            std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                                            std::span<const uint8_t> password,
                                            std::span<const uint8_t> keyfile,
                                            V3Header& replacement) noexcept;
[[nodiscard]] std::array<uint8_t, V3_MASTER_WRAP_AD_SIZE>
master_wrap_ad(const V3Header& header) noexcept;
[[nodiscard]] HeaderStatus
unwrap_v3_master_key(const V3Header& header, std::span<const uint8_t> password,
                     std::span<const uint8_t> keyfile,
                     crypto::SecureBuffer<crypto::KEY_SIZE>& master_key) noexcept;

}  // namespace vault::v3
