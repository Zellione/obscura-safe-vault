#include "vault/v3_sqlcipher_key.h"

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
        out[2 + i * 2] = static_cast<uint8_t>(HEX[key[i] >> 4]);
        out[3 + i * 2] = static_cast<uint8_t>(HEX[key[i] & 0x0f]);
    }
    out[out.size() - 1] = '\'';
    return result;
}

} // namespace vault::v3
