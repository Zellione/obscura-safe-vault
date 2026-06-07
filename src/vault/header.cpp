#include "header.h"

#include <cstring>

#include "byte_io.h"

namespace vault {

// Fixed byte offsets, reproduced exactly from the spec table in CLAUDE.md.
// NOTE: the spec places slot B at offset 166, leaving an 8-byte reserved gap
// after slot A's nonce (which ends at 158). We follow the documented offsets
// verbatim so the on-disk layout matches the authoritative spec.
namespace off {
inline constexpr size_t MAGIC              = 0;    // 8
inline constexpr size_t VERSION            = 8;    // 2
inline constexpr size_t HEADER_SIZE_F      = 10;   // 2
inline constexpr size_t FLAGS              = 12;   // 4
inline constexpr size_t KDF_ALGO           = 16;   // 1
inline constexpr size_t T_COST             = 17;   // 4
inline constexpr size_t M_COST_KIB         = 21;   // 4
inline constexpr size_t PARALLELISM        = 25;   // 4
inline constexpr size_t SALT               = 29;   // 16
inline constexpr size_t KEYFILE_REQUIRED   = 45;   // 1
inline constexpr size_t MK_NONCE           = 46;   // 24
inline constexpr size_t WRAPPED_MASTER_KEY = 70;   // 32
inline constexpr size_t MK_TAG             = 102;  // 16
inline constexpr size_t SLOT_A_OFFSET      = 118;  // 8
inline constexpr size_t SLOT_A_LENGTH      = 126;  // 8
inline constexpr size_t SLOT_A_NONCE       = 134;  // 24  (ends at 158; 158..165 reserved)
inline constexpr size_t SLOT_B_OFFSET      = 166;  // 8
inline constexpr size_t SLOT_B_LENGTH      = 174;  // 8
inline constexpr size_t SLOT_B_NONCE       = 182;  // 24
inline constexpr size_t ACTIVE_SLOT        = 206;  // 1
} // namespace off

void Header::serialize(std::span<uint8_t, HEADER_SIZE> out) const noexcept
{
    std::memset(out.data(), 0, out.size());  // zero padding + reserved gaps

    std::memcpy(out.data() + off::MAGIC, MAGIC, sizeof(MAGIC));
    put_u16_at(out, off::VERSION,       version);
    put_u16_at(out, off::HEADER_SIZE_F, static_cast<uint16_t>(HEADER_SIZE));
    put_u32_at(out, off::FLAGS,         flags);

    out[off::KDF_ALGO] = kdf_algo;
    put_u32_at(out, off::T_COST,      kdf.t_cost);
    put_u32_at(out, off::M_COST_KIB,  kdf.m_cost_kib);
    put_u32_at(out, off::PARALLELISM, kdf.parallelism);
    put_bytes_at(out, off::SALT, salt);
    out[off::KEYFILE_REQUIRED] = keyfile_required;

    put_bytes_at(out, off::MK_NONCE,           mk_nonce);
    put_bytes_at(out, off::WRAPPED_MASTER_KEY, wrapped_master_key);
    put_bytes_at(out, off::MK_TAG,             mk_tag);

    put_u64_at(out, off::SLOT_A_OFFSET, slot[0].offset);
    put_u64_at(out, off::SLOT_A_LENGTH, slot[0].length);
    put_bytes_at(out, off::SLOT_A_NONCE, slot[0].nonce);
    put_u64_at(out, off::SLOT_B_OFFSET, slot[1].offset);
    put_u64_at(out, off::SLOT_B_LENGTH, slot[1].length);
    put_bytes_at(out, off::SLOT_B_NONCE, slot[1].nonce);

    out[off::ACTIVE_SLOT] = active_slot;
}

bool Header::parse(std::span<const uint8_t> raw, Header& out) noexcept
{
    if (raw.size() < HEADER_SIZE) return false;
    if (std::memcmp(raw.data() + off::MAGIC, MAGIC, sizeof(MAGIC)) != 0) return false;

    out.version = get_u16_at(raw, off::VERSION);
    if (out.version != FORMAT_VERSION) return false;

    out.header_size = get_u16_at(raw, off::HEADER_SIZE_F);
    out.flags       = get_u32_at(raw, off::FLAGS);

    out.kdf_algo        = raw[off::KDF_ALGO];
    out.kdf.t_cost      = get_u32_at(raw, off::T_COST);
    out.kdf.m_cost_kib  = get_u32_at(raw, off::M_COST_KIB);
    out.kdf.parallelism = get_u32_at(raw, off::PARALLELISM);
    std::memcpy(out.salt.data(), raw.data() + off::SALT, out.salt.size());
    out.keyfile_required = raw[off::KEYFILE_REQUIRED];

    std::memcpy(out.mk_nonce.data(), raw.data() + off::MK_NONCE, out.mk_nonce.size());
    std::memcpy(out.wrapped_master_key.data(), raw.data() + off::WRAPPED_MASTER_KEY,
                out.wrapped_master_key.size());
    std::memcpy(out.mk_tag.data(), raw.data() + off::MK_TAG, out.mk_tag.size());

    out.slot[0].offset = get_u64_at(raw, off::SLOT_A_OFFSET);
    out.slot[0].length = get_u64_at(raw, off::SLOT_A_LENGTH);
    std::memcpy(out.slot[0].nonce.data(), raw.data() + off::SLOT_A_NONCE,
                out.slot[0].nonce.size());
    out.slot[1].offset = get_u64_at(raw, off::SLOT_B_OFFSET);
    out.slot[1].length = get_u64_at(raw, off::SLOT_B_LENGTH);
    std::memcpy(out.slot[1].nonce.data(), raw.data() + off::SLOT_B_NONCE,
                out.slot[1].nonce.size());

    out.active_slot = raw[off::ACTIVE_SLOT];
    return true;
}

} // namespace vault
