#include "v3_crypto_spec.h"

#include <monocypher.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace vault::v3 {
namespace {

template <typename T>
void put_le(uint8_t*& out, T value) noexcept
{
    for (size_t i = 0; i < sizeof(T); ++i) {
        *out++ = static_cast<uint8_t>(value & 0xffU);
        value >>= 8U;
    }
}

template <size_t N>
void put_bytes(uint8_t*& out, std::span<const uint8_t, N> bytes) noexcept
{
    out = std::copy(bytes.begin(), bytes.end(), out);
}

template <size_t N>
void put_ascii(uint8_t*& out, const char (&text)[N]) noexcept
{
    out = std::copy_n(reinterpret_cast<const uint8_t*>(text), N, out);
}

} // namespace

crypto::SecureBuffer<crypto::KEY_SIZE>
derive_database_key(std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                    std::span<const uint8_t, crypto::NODE_ID_SIZE> vault_id) noexcept
{
    constexpr char DOMAIN[] = "OSV-V3-DB-KEY";
    std::array<uint8_t, sizeof(DOMAIN) + crypto::NODE_ID_SIZE + 2> input{};
    uint8_t* out = input.data();
    put_ascii(out, DOMAIN);
    put_bytes(out, vault_id);
    put_le(out, FORMAT_VERSION);

    crypto::SecureBuffer<crypto::KEY_SIZE> key;
    crypto_blake2b_keyed(key.data(), key.size(), master_key.data(), master_key.size(),
                         input.data(), input.size());
    crypto_wipe(input.data(), input.size());
    return key;
}

crypto::SecureBuffer<crypto::KEY_SIZE>
derive_object_key(std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                  std::span<const uint8_t, crypto::NODE_ID_SIZE> vault_id,
                  std::span<const uint8_t, crypto::NODE_ID_SIZE> object_id) noexcept
{
    constexpr char DOMAIN[] = "OSV-V3-OBJECT-KEY";
    std::array<uint8_t, sizeof(DOMAIN) + crypto::NODE_ID_SIZE * 2 + 2> input{};
    uint8_t* out = input.data();
    put_ascii(out, DOMAIN);
    put_bytes(out, vault_id);
    put_bytes(out, object_id);
    put_le(out, OBJECT_FORMAT_VERSION);

    crypto::SecureBuffer<crypto::KEY_SIZE> key;
    crypto_blake2b_keyed(key.data(), key.size(), master_key.data(), master_key.size(),
                         input.data(), input.size());
    crypto_wipe(input.data(), input.size());
    return key;
}

std::array<uint8_t, OBJECT_FRAME_AD_SIZE>
build_object_frame_ad(const ObjectFrameTag& tag) noexcept
{
    std::array<uint8_t, OBJECT_FRAME_AD_SIZE> ad{};
    uint8_t* out = ad.data();
    *out++ = static_cast<uint8_t>(tag.role);
    *out++ = OBJECT_AD_VERSION;
    put_bytes(out, std::span<const uint8_t, crypto::NODE_ID_SIZE>{tag.vault_id});
    put_bytes(out, std::span<const uint8_t, crypto::NODE_ID_SIZE>{tag.object_id});
    put_bytes(out, std::span<const uint8_t, crypto::NODE_ID_SIZE>{tag.owner_node_id});
    put_le(out, tag.frame_index);
    put_le(out, tag.frame_count);
    put_le(out, tag.total_plaintext_length);
    return ad;
}

} // namespace vault::v3

