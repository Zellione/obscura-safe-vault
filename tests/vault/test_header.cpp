#include "test_framework.h"

#include <array>
#include <cstring>
#include <vector>

#include "crypto/crypto.h"
#include "vault/header.h"

// Build a header with recognisable values in every field so a round-trip can
// prove each one survives serialize -> parse.
static vault::Header sample_header()
{
    vault::Header h;
    h.version          = vault::FORMAT_VERSION;
    h.flags            = 0xDEADBEEF;
    h.kdf_algo         = 0;
    h.kdf              = {.t_cost = 4, .m_cost_kib = 131072, .parallelism = 2};
    h.keyfile_required = 1;
    for (size_t i = 0; i < h.salt.size(); ++i)  h.salt[i]  = static_cast<uint8_t>(i + 1);
    for (size_t i = 0; i < h.mk_nonce.size(); ++i) h.mk_nonce[i] = static_cast<uint8_t>(0x10 + i);
    for (size_t i = 0; i < h.wrapped_master_key.size(); ++i)
        h.wrapped_master_key[i] = static_cast<uint8_t>(0x40 + i);
    for (size_t i = 0; i < h.mk_tag.size(); ++i) h.mk_tag[i] = static_cast<uint8_t>(0x90 + i);

    h.slot[0] = {.offset = 4096, .length = 200, .nonce = {}};
    h.slot[1] = {.offset = 8192, .length = 333, .nonce = {}};
    for (size_t i = 0; i < crypto::NONCE_SIZE; ++i) {
        h.slot[0].nonce[i] = static_cast<uint8_t>(0xA0 + i);
        h.slot[1].nonce[i] = static_cast<uint8_t>(0xB0 + i);
    }
    h.active_slot = 1;
    return h;
}

TEST(header_roundtrip_preserves_all_fields)
{
    vault::Header in = sample_header();

    std::array<uint8_t, vault::HEADER_SIZE> raw{};
    in.serialize(raw);

    vault::Header out;
    REQUIRE(vault::Header::parse(raw, out));

    CHECK_EQ(out.version, in.version);
    CHECK_EQ(out.header_size, static_cast<uint16_t>(vault::HEADER_SIZE));
    CHECK_EQ(out.flags, in.flags);
    CHECK_EQ(out.kdf_algo, in.kdf_algo);
    CHECK_EQ(out.kdf.t_cost, in.kdf.t_cost);
    CHECK_EQ(out.kdf.m_cost_kib, in.kdf.m_cost_kib);
    CHECK_EQ(out.kdf.parallelism, in.kdf.parallelism);
    CHECK_EQ(out.keyfile_required, in.keyfile_required);
    CHECK_BYTES_EQ(std::span<const uint8_t>(out.salt), std::span<const uint8_t>(in.salt));
    CHECK_BYTES_EQ(std::span<const uint8_t>(out.mk_nonce), std::span<const uint8_t>(in.mk_nonce));
    CHECK_BYTES_EQ(std::span<const uint8_t>(out.wrapped_master_key),
                   std::span<const uint8_t>(in.wrapped_master_key));
    CHECK_BYTES_EQ(std::span<const uint8_t>(out.mk_tag), std::span<const uint8_t>(in.mk_tag));

    CHECK_EQ(out.slot[0].offset, in.slot[0].offset);
    CHECK_EQ(out.slot[0].length, in.slot[0].length);
    CHECK_BYTES_EQ(std::span<const uint8_t>(out.slot[0].nonce),
                   std::span<const uint8_t>(in.slot[0].nonce));
    CHECK_EQ(out.slot[1].offset, in.slot[1].offset);
    CHECK_EQ(out.slot[1].length, in.slot[1].length);
    CHECK_BYTES_EQ(std::span<const uint8_t>(out.slot[1].nonce),
                   std::span<const uint8_t>(in.slot[1].nonce));
    CHECK_EQ(out.active_slot, in.active_slot);
}

TEST(header_writes_magic_at_offset_zero)
{
    vault::Header in = sample_header();
    std::array<uint8_t, vault::HEADER_SIZE> raw{};
    in.serialize(raw);
    CHECK_EQ(std::memcmp(raw.data(), vault::MAGIC, sizeof(vault::MAGIC)), 0);
}

TEST(header_parse_rejects_bad_magic)
{
    vault::Header in = sample_header();
    std::array<uint8_t, vault::HEADER_SIZE> raw{};
    in.serialize(raw);
    raw[0] ^= 0xFF;  // corrupt magic

    vault::Header out;
    CHECK_FALSE(vault::Header::parse(raw, out));
}

TEST(header_parse_rejects_unsupported_version)
{
    vault::Header in = sample_header();
    std::array<uint8_t, vault::HEADER_SIZE> raw{};
    in.serialize(raw);
    raw[8] = 0xFF;  // version low byte -> 0xFF.. (unsupported)
    raw[9] = 0xFF;

    vault::Header out;
    CHECK_FALSE(vault::Header::parse(raw, out));
}

TEST(header_parse_rejects_short_buffer)
{
    std::vector<uint8_t> tiny(10, 0);
    vault::Header out;
    CHECK_FALSE(vault::Header::parse(tiny, out));
}
