#include "vault/v3_header.h"

#include "crypto/aead.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace vault::v3 {
namespace {

uint16_t get16(std::span<const uint8_t> bytes, size_t off) noexcept
{
    return static_cast<uint16_t>(bytes[off]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[off + 1]) << 8);
}

uint32_t get32(std::span<const uint8_t> bytes, size_t off) noexcept
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<uint32_t>(bytes[off + i]) << (i * 8);
    return value;
}

void put16(std::span<uint8_t> bytes, size_t off, uint16_t value) noexcept
{
    bytes[off] = static_cast<uint8_t>(value);
    bytes[off + 1] = static_cast<uint8_t>(value >> 8);
}

void put32(std::span<uint8_t> bytes, size_t off, uint32_t value) noexcept
{
    for (unsigned i = 0; i < 4; ++i) bytes[off + i] = static_cast<uint8_t>(value >> (i * 8));
}

bool nonzero(const Id& id) noexcept
{
    return std::ranges::any_of(id, [](uint8_t byte) { return byte != 0; });
}

} // namespace

HeaderStatus parse_v3_header(std::span<const uint8_t, V3_HEADER_SIZE> raw,
                             V3Header& out) noexcept
{
    constexpr std::array<uint8_t, 8> MAGIC{'O','S','V','3','D','I','R',0};
    if (!std::ranges::equal(MAGIC, raw.first<8>())) return HeaderStatus::Invalid;
    if (get16(raw, 8) != FORMAT_VERSION) return HeaderStatus::UnsupportedVersion;
    if (get16(raw, 10) != V3_HEADER_SIZE) return HeaderStatus::Invalid;
    const uint32_t flags = get32(raw, 12);
    if (flags != 0) return HeaderStatus::UnsupportedVersion;
    if (raw[16] != 0 || raw[45] > 1) return HeaderStatus::Invalid;
    const crypto::KdfParams kdf{get32(raw, 17), get32(raw, 21), get32(raw, 25)};
    if (kdf.t_cost == 0 || kdf.t_cost > crypto::MAX_KDF_T_COST || kdf.m_cost_kib < 8 ||
        kdf.m_cost_kib > crypto::MAX_KDF_M_COST_KIB || kdf.parallelism == 0 ||
        kdf.parallelism > crypto::MAX_KDF_PARALLELISM)
        return HeaderStatus::Invalid;
    if (std::ranges::any_of(raw.subspan(134), [](uint8_t byte) { return byte != 0; }))
        return HeaderStatus::Invalid;
    V3Header parsed;
    parsed.flags = flags;
    parsed.kdf = kdf;
    parsed.keyfile_required = raw[45] != 0;
    std::copy_n(raw.begin() + 29, parsed.salt.size(), parsed.salt.begin());
    std::copy_n(raw.begin() + 46, parsed.nonce.size(), parsed.nonce.begin());
    std::copy_n(raw.begin() + 70, parsed.wrapped_master_key.size(),
                parsed.wrapped_master_key.begin());
    std::copy_n(raw.begin() + 102, parsed.tag.size(), parsed.tag.begin());
    std::copy_n(raw.begin() + 118, parsed.vault_id.size(), parsed.vault_id.begin());
    if (!nonzero(parsed.vault_id)) return HeaderStatus::Invalid;
    out = parsed;
    return HeaderStatus::Ok;
}

std::array<uint8_t, V3_MASTER_WRAP_AD_SIZE> master_wrap_ad(const V3Header& header) noexcept
{
    std::array<uint8_t, V3_MASTER_WRAP_AD_SIZE> ad{};
    ad[0] = 0x10;
    ad[1] = 1;
    put16(ad, 2, FORMAT_VERSION);
    std::copy(header.vault_id.begin(), header.vault_id.end(), ad.begin() + 4);
    put16(ad, 20, V3_HEADER_SIZE);
    put32(ad, 22, header.flags);
    return ad;
}

HeaderStatus unwrap_v3_master_key(const V3Header& header,
                                  std::span<const uint8_t> password,
                                  std::span<const uint8_t> keyfile,
                                  crypto::SecureBuffer<crypto::KEY_SIZE>& master_key) noexcept
{
    if (header.keyfile_required && keyfile.empty()) return HeaderStatus::AuthenticationFailed;
    crypto::SecureBuffer<crypto::KEY_SIZE> kek;
    if (!crypto::derive_key(password, keyfile, header.salt, header.kdf, kek))
        return HeaderStatus::CryptoError;
    std::array<uint8_t, crypto::KEY_SIZE + crypto::TAG_SIZE> sealed{};
    std::copy(header.wrapped_master_key.begin(), header.wrapped_master_key.end(), sealed.begin());
    std::copy(header.tag.begin(), header.tag.end(), sealed.begin() + crypto::KEY_SIZE);
    const auto ad = master_wrap_ad(header);
    if (!crypto::open_to(kek.as_span(), header.nonce, sealed, master_key.span(), ad)) {
        master_key.wipe();
        return HeaderStatus::AuthenticationFailed;
    }
    return HeaderStatus::Ok;
}

} // namespace vault::v3
