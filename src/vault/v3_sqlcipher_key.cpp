#include "vault/v3_sqlcipher_key.h"

#include <cstddef>

namespace vault::v3 {

crypto::SecureBuffer<SQLCIPHER_RAW_KEYSPEC_SIZE>
sqlcipher_raw_keyspec(std::span<const uint8_t, crypto::KEY_SIZE> key) noexcept
{
    static constexpr char HEX[] = "0123456789abcdef";
    crypto::SecureBuffer<SQLCIPHER_RAW_KEYSPEC_SIZE> result;
    auto out = result.span();
    out[0] = 'x';
    out[1] = '\'';
    for (size_t i = 0; i < key.size(); ++i) {
        const auto byte = static_cast<std::byte>(key[i]);
        out[2 + i * 2] = static_cast<uint8_t>(HEX[std::to_integer<uint8_t>(byte >> 4)]);
        out[3 + i * 2] =
            static_cast<uint8_t>(HEX[std::to_integer<uint8_t>(byte & std::byte{0x0f})]);
    }
    out[out.size() - 1] = '\'';
    return result;
}

}  // namespace vault::v3
