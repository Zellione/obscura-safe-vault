#include "vault/v3_header.h"

#include "crypto/aead.h"
#include "crypto/random.h"

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
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<uint32_t>(bytes[off + i]) << (i * 8);
    return value;
}

void put16(std::span<uint8_t> bytes, size_t off, uint16_t value) noexcept
{
    bytes[off] = static_cast<uint8_t>(value);
    bytes[off + 1] = static_cast<uint8_t>(value >> 8);
}

void put32(std::span<uint8_t> bytes, size_t off, uint32_t value) noexcept
{
    for (unsigned i = 0; i < 4; ++i)
        bytes[off + i] = static_cast<uint8_t>(value >> (i * 8));
}

bool nonzero(const Id& id) noexcept
{
    return std::ranges::any_of(id, [](uint8_t byte) { return byte != 0; });
}

}  // namespace

HeaderStatus parse_v3_header(std::span<const uint8_t, V3_HEADER_SIZE> raw, V3Header& out) noexcept
{
    using enum HeaderStatus;
    if (constexpr std::array<uint8_t, 8> MAGIC{'O', 'S', 'V', '3', 'D', 'I', 'R', 0};
        !std::ranges::equal(MAGIC, raw.first<8>()))
        return Invalid;
    if (get16(raw, 8) != FORMAT_VERSION) return UnsupportedVersion;
    if (get16(raw, 10) != V3_HEADER_SIZE) return Invalid;
    const uint32_t flags = get32(raw, 12);
    if (flags != 0) return UnsupportedVersion;
    if (raw[16] != 0 || raw[45] > 1) return Invalid;
    const crypto::KdfParams kdf{get32(raw, 17), get32(raw, 21), get32(raw, 25)};
    if (kdf.t_cost == 0 || kdf.t_cost > crypto::MAX_KDF_T_COST || kdf.m_cost_kib < 8 ||
        kdf.m_cost_kib > crypto::MAX_KDF_M_COST_KIB || kdf.parallelism == 0 ||
        kdf.parallelism > crypto::MAX_KDF_PARALLELISM)
        return Invalid;
    if (std::ranges::any_of(raw.subspan(134), [](uint8_t byte) { return byte != 0; }))
        return Invalid;
    V3Header parsed;
    parsed.flags = flags;
    parsed.kdf = kdf;
    parsed.keyfile_required = raw[45] != 0;
    std::ranges::copy(raw.subspan(29, parsed.salt.size()), parsed.salt.begin());
    std::ranges::copy(raw.subspan(46, parsed.nonce.size()), parsed.nonce.begin());
    std::ranges::copy(raw.subspan(70, parsed.wrapped_master_key.size()),
                      parsed.wrapped_master_key.begin());
    std::ranges::copy(raw.subspan(102, parsed.tag.size()), parsed.tag.begin());
    std::ranges::copy(raw.subspan(118, parsed.vault_id.size()), parsed.vault_id.begin());
    if (!nonzero(parsed.vault_id)) return Invalid;
    out = parsed;
    return Ok;
}

std::array<uint8_t, V3_HEADER_SIZE> serialize_v3_header(const V3Header& header) noexcept
{
    std::array<uint8_t, V3_HEADER_SIZE> raw{};
    constexpr std::array<uint8_t, 8> MAGIC{'O', 'S', 'V', '3', 'D', 'I', 'R', 0};
    std::ranges::copy(MAGIC, raw.begin());
    put16(raw, 8, FORMAT_VERSION);
    put16(raw, 10, V3_HEADER_SIZE);
    put32(raw, 12, header.flags);
    raw[16] = 0;  // Argon2id
    put32(raw, 17, header.kdf.t_cost);
    put32(raw, 21, header.kdf.m_cost_kib);
    put32(raw, 25, header.kdf.parallelism);
    std::ranges::copy(header.salt, raw.begin() + 29);
    raw[45] = header.keyfile_required ? 1 : 0;
    std::ranges::copy(header.nonce, raw.begin() + 46);
    std::ranges::copy(header.wrapped_master_key, raw.begin() + 70);
    std::ranges::copy(header.tag, raw.begin() + 102);
    std::ranges::copy(header.vault_id, raw.begin() + 118);
    return raw;
}

HeaderStatus create_v3_header(std::span<const uint8_t> password, std::span<const uint8_t> keyfile,
                              const crypto::KdfParams& kdf, V3Header& header,
                              crypto::SecureBuffer<crypto::KEY_SIZE>& master_key) noexcept
{
    using enum HeaderStatus;
    V3Header created;
    created.kdf = kdf;
    created.keyfile_required = !keyfile.empty();
    if (!crypto::fill_random(created.salt) || !crypto::fill_random(created.nonce) ||
        !crypto::fill_random(created.vault_id) || !crypto::fill_random(master_key.span()))
        return CryptoError;
    crypto::SecureBuffer<crypto::KEY_SIZE> kek;
    if (!crypto::derive_key(password, keyfile, created.salt, kdf, kek)) {
        master_key.wipe();
        return CryptoError;
    }
    std::vector<uint8_t> sealed;
    if (const auto ad = master_wrap_ad(created);
        !crypto::seal(kek.as_span(), created.nonce, master_key.as_span(), sealed, ad)) {
        master_key.wipe();
        return CryptoError;
    }
    std::ranges::copy_n(sealed.begin(), crypto::KEY_SIZE, created.wrapped_master_key.begin());
    std::ranges::copy_n(sealed.begin() + crypto::KEY_SIZE, crypto::TAG_SIZE, created.tag.begin());
    header = created;
    return Ok;
}

HeaderStatus rewrap_v3_header(const V3Header& current,
                              std::span<const uint8_t, crypto::KEY_SIZE> master_key,
                              std::span<const uint8_t> password, std::span<const uint8_t> keyfile,
                              V3Header& replacement) noexcept
{
    using enum HeaderStatus;
    V3Header next = current;
    next.keyfile_required = !keyfile.empty();
    if (!crypto::fill_random(next.salt) || !crypto::fill_random(next.nonce)) return CryptoError;
    crypto::SecureBuffer<crypto::KEY_SIZE> kek;
    if (!crypto::derive_key(password, keyfile, next.salt, next.kdf, kek)) return CryptoError;
    std::vector<uint8_t> sealed;
    if (const auto ad = master_wrap_ad(next);
        !crypto::seal(kek.as_span(), next.nonce, master_key, sealed, ad))
        return CryptoError;
    std::ranges::copy_n(sealed.begin(), crypto::KEY_SIZE, next.wrapped_master_key.begin());
    std::ranges::copy_n(sealed.begin() + crypto::KEY_SIZE, crypto::TAG_SIZE, next.tag.begin());
    replacement = next;
    return Ok;
}

std::array<uint8_t, V3_MASTER_WRAP_AD_SIZE> master_wrap_ad(const V3Header& header) noexcept
{
    std::array<uint8_t, V3_MASTER_WRAP_AD_SIZE> ad{};
    ad[0] = 0x10;
    ad[1] = 1;
    put16(ad, 2, FORMAT_VERSION);
    std::ranges::copy(header.vault_id, ad.begin() + 4);
    put16(ad, 20, V3_HEADER_SIZE);
    put32(ad, 22, header.flags);
    return ad;
}

HeaderStatus unwrap_v3_master_key(const V3Header& header, std::span<const uint8_t> password,
                                  std::span<const uint8_t> keyfile,
                                  crypto::SecureBuffer<crypto::KEY_SIZE>& master_key) noexcept
{
    using enum HeaderStatus;
    if (header.keyfile_required && keyfile.empty()) return AuthenticationFailed;
    crypto::SecureBuffer<crypto::KEY_SIZE> kek;
    if (!crypto::derive_key(password, keyfile, header.salt, header.kdf, kek)) return CryptoError;
    std::array<uint8_t, crypto::KEY_SIZE + crypto::TAG_SIZE> sealed{};
    std::ranges::copy(header.wrapped_master_key, sealed.begin());
    std::ranges::copy(header.tag, sealed.begin() + crypto::KEY_SIZE);
    if (const auto ad = master_wrap_ad(header);
        !crypto::open_to(kek.as_span(), header.nonce, sealed, master_key.span(), ad)) {
        master_key.wipe();
        return AuthenticationFailed;
    }
    return Ok;
}

}  // namespace vault::v3
