#include "test_framework.h"

#include <array>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

#include <monocypher.h>

#include "crypto/aead.h"
#include "crypto/random.h"
#include "crypto/secure_mem.h"

// XChaCha20-Poly1305 known-answer test from draft-irtf-cfrg-xchacha-03 A.3.1.
// Monocypher's 24-byte-nonce crypto_aead_lock is this exact construction.
TEST(xchacha20poly1305_draft_kat)
{
    constexpr std::string_view plaintext_str =
        "Ladies and Gentlemen of the class of '99: If I could offer you "
        "only one tip for the future, sunscreen would be it.";
    static_assert(plaintext_str.size() == 114);
    const size_t plen = plaintext_str.size();  // 114 bytes

    std::array<uint8_t, 12> ad = {
        0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7};

    std::array<uint8_t, 32> key;
    for (size_t i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(0x80 + i);

    std::array<uint8_t, 24> nonce;
    for (size_t i = 0; i < 24; ++i) nonce[i] = static_cast<uint8_t>(0x40 + i);

    static constexpr std::array<uint8_t, 114> expected_cipher = {{
        0xbd, 0x6d, 0x17, 0x9d, 0x3e, 0x83, 0xd4, 0x3b, 0x95, 0x76, 0x57, 0x94,
        0x93, 0xc0, 0xe9, 0x39, 0x57, 0x2a, 0x17, 0x00, 0x25, 0x2b, 0xfa, 0xcc,
        0xbe, 0xd2, 0x90, 0x2c, 0x21, 0x39, 0x6c, 0xbb, 0x73, 0x1c, 0x7f, 0x1b,
        0x0b, 0x4a, 0xa6, 0x44, 0x0b, 0xf3, 0xa8, 0x2f, 0x4e, 0xda, 0x7e, 0x39,
        0xae, 0x64, 0xc6, 0x70, 0x8c, 0x54, 0xc2, 0x16, 0xcb, 0x96, 0xb7, 0x2e,
        0x12, 0x13, 0xb4, 0x52, 0x2f, 0x8c, 0x9b, 0xa4, 0x0d, 0xb5, 0xd9, 0x45,
        0xb1, 0x1b, 0x69, 0xb9, 0x82, 0xc1, 0xbb, 0x9e, 0x3f, 0x3f, 0xac, 0x2b,
        0xc3, 0x69, 0x48, 0x8f, 0x76, 0xb2, 0x38, 0x35, 0x65, 0xd3, 0xff, 0xf9,
        0x21, 0xf9, 0x66, 0x4c, 0x97, 0x63, 0x7d, 0xa9, 0x76, 0x88, 0x12, 0xf6,
        0x15, 0xc6, 0x8b, 0x13, 0xb5, 0x2e}};
    static constexpr std::array<uint8_t, 16> expected_tag = {{
        0xc0, 0x87, 0x59, 0x24, 0xc1, 0xc7, 0x98, 0x79,
        0x47, 0xde, 0xaf, 0xd8, 0x78, 0x0a, 0xcf, 0x49}};

    std::vector<uint8_t> cipher(plen);
    std::array<uint8_t, 16> tag{};
    crypto_aead_lock(cipher.data(), tag.data(), key.data(), nonce.data(),
                     ad.data(), ad.size(),
                     reinterpret_cast<const uint8_t*>(plaintext_str.data()), plen);

    CHECK_BYTES_EQ(std::span<const uint8_t>(cipher),
                   std::span<const uint8_t>(expected_cipher.data(), plen));
    CHECK_BYTES_EQ(std::span<const uint8_t>(tag),
                   std::span<const uint8_t>(expected_tag));
}

static crypto::SecureBuffer<crypto::KEY_SIZE> random_key()
{
    crypto::SecureBuffer<crypto::KEY_SIZE> k;
    (void)crypto::fill_random(k.span());
    return k;
}

TEST(encrypt_decrypt_roundtrip)
{
    auto key = random_key();

    const std::array<size_t, 4> sizes = {0, 1, 1024, 1u << 20};  // incl. empty + 1 MiB
    for (size_t n : sizes) {
        std::vector<uint8_t> plain(n);
        for (size_t i = 0; i < n; ++i) plain[i] = static_cast<uint8_t>(i * 31 + 7);

        std::vector<uint8_t> chunk;
        REQUIRE(crypto::encrypt_chunk(key.as_span(), plain, chunk));
        CHECK_EQ(chunk.size(), n + crypto::NONCE_SIZE + crypto::TAG_SIZE);

        std::vector<uint8_t> recovered;
        REQUIRE(crypto::decrypt_chunk(key.as_span(), chunk, recovered));
        CHECK_BYTES_EQ(std::span<const uint8_t>(recovered),
                       std::span<const uint8_t>(plain));
    }
}

TEST(decrypt_with_associated_data)
{
    auto key = random_key();
    std::array<uint8_t, 4> plain = {1, 2, 3, 4};
    std::array<uint8_t, 5> ad    = {9, 8, 7, 6, 5};

    std::vector<uint8_t> chunk;
    REQUIRE(crypto::encrypt_chunk(key.as_span(), plain, chunk, ad));

    std::vector<uint8_t> recovered;
    // Correct AD -> success.
    REQUIRE(crypto::decrypt_chunk(key.as_span(), chunk, recovered, ad));
    CHECK_BYTES_EQ(std::span<const uint8_t>(recovered), std::span<const uint8_t>(plain));

    // Wrong AD -> authentication failure.
    std::array<uint8_t, 5> wrong_ad = {9, 8, 7, 6, 4};
    CHECK_FALSE(crypto::decrypt_chunk(key.as_span(), chunk, recovered, wrong_ad));
    CHECK_TRUE(recovered.empty());
}

TEST(tamper_detection)
{
    auto key = random_key();
    std::array<uint8_t, 16> plain; plain.fill(0x42);

    std::vector<uint8_t> chunk;
    REQUIRE(crypto::encrypt_chunk(key.as_span(), plain, chunk));

    // Flip a byte in each region; every variant must fail to decrypt.
    const size_t cipher_off = crypto::NONCE_SIZE;
    const size_t tag_off    = chunk.size() - crypto::TAG_SIZE;
    const std::array<size_t, 3> positions = {0 /*nonce*/, cipher_off /*ciphertext*/, tag_off /*tag*/};

    for (size_t pos : positions) {
        std::vector<uint8_t> bad = chunk;
        bad[pos] ^= uint8_t{0x01};
        std::vector<uint8_t> out;
        CHECK_FALSE(crypto::decrypt_chunk(key.as_span(), bad, out));
        CHECK_TRUE(out.empty());
    }
}

TEST(short_chunk_rejected)
{
    auto key = random_key();
    std::vector<uint8_t> too_short(crypto::NONCE_SIZE + crypto::TAG_SIZE - 1, 0);
    std::vector<uint8_t> out;
    CHECK_FALSE(crypto::decrypt_chunk(key.as_span(), too_short, out));
}

TEST(nonce_is_fresh_per_call)
{
    auto key = random_key();
    std::array<uint8_t, 8> plain; plain.fill(0xEE);

    std::vector<uint8_t> a;
    std::vector<uint8_t> b;
    REQUIRE(crypto::encrypt_chunk(key.as_span(), plain, a));
    REQUIRE(crypto::encrypt_chunk(key.as_span(), plain, b));

    // Same plaintext, but fresh random nonces -> different nonce AND ciphertext.
    CHECK_FALSE(::testing::bytes_equal(
        std::span<const uint8_t>(a.data(), crypto::NONCE_SIZE),
        std::span<const uint8_t>(b.data(), crypto::NONCE_SIZE)));
    CHECK_FALSE(::testing::bytes_equal(std::span<const uint8_t>(a),
                                       std::span<const uint8_t>(b)));
}

TEST(encrypt_rejects_unrepresentable_output_size_without_terminating)
{
    auto key = random_key();
    std::vector<uint8_t> out = {0xAA};
    const std::span<const uint8_t> impossible(
        static_cast<const uint8_t*>(nullptr), std::numeric_limits<size_t>::max());

    CHECK_FALSE(crypto::encrypt_chunk(key.as_span(), impossible, out));
    CHECK_TRUE(out.empty());
}

// --- Phase 99 / OSV-AUD-004: context-bound AEAD associated data -------------

// Exact-byte KAT for the canonical AD encoding. Locks the byte layout so a
// change silently breaking Linux/Windows parity (or vault reopening) is caught.
TEST(build_chunk_ad_exact_bytes)
{
    crypto::ChunkTag t;
    t.domain = crypto::ChunkDomain::Video;
    for (size_t i = 0; i < t.owner.size(); ++i)    t.owner[i]  = static_cast<uint8_t>(0xA0 + i);
    for (size_t i = 0; i < t.record.size(); ++i)   t.record[i] = static_cast<uint8_t>(0x10 + i);
    t.sequence      = 0xDEADBEEF;
    t.context_bound = true;

    const auto ad = crypto::build_chunk_ad(t);
    CHECK_EQ(ad.size(), crypto::AD_SIZE);
    static constexpr std::array<uint8_t, crypto::AD_SIZE> expected = {{
        0x03,                       // domain = Video
        0x01,                       // CHUNK_AD_VERSION
        0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,   // owner
        0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,   // record
        0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,
        0xEF,0xBE,0xAD,0xDE,                       // sequence 0xDEADBEEF LE
    }};
    CHECK_BYTES_EQ(std::span<const uint8_t>(ad), std::span<const uint8_t>(expected));
}

TEST(build_chunk_ad_is_fixed_width_across_empty_fields)
{
    crypto::ChunkTag t;   // all fields default/zero
    CHECK_EQ(crypto::build_chunk_ad(t).size(), crypto::AD_SIZE);
    CHECK_EQ(crypto::build_chunk_ad(t)[0], static_cast<uint8_t>(crypto::ChunkDomain::Data));
}

// Same key + nonce + plaintext under different domains must not authenticate
// against the other domain's AD. encrypt_chunk uses a fresh random nonce per
// call; to compare like-for-like we seal the identical (key, nonce, plaintext)
// under two ADs via crypto::seal and confirm both the tags differ and each
// opens under its own AD only.
TEST(context_domains_produce_distinct_tags)
{
    auto key = random_key();
    std::array<uint8_t, crypto::NONCE_SIZE> nonce;
    (void)crypto::fill_random(nonce);
    std::array<uint8_t, 16> plain;
    plain.fill(0x42);

    auto make_tag = [](crypto::ChunkDomain d) {
        crypto::ChunkTag t;
        t.domain = d;
        t.context_bound = true;
        for (size_t i = 0; i < t.owner.size(); ++i) t.owner[i] = static_cast<uint8_t>(i * 7 + 1);
        for (size_t i = 0; i < t.record.size(); ++i) t.record[i] = static_cast<uint8_t>(i * 3 + 5);
        return t;
    };

    const auto ad_data = crypto::build_chunk_ad(make_tag(crypto::ChunkDomain::Data));
    const auto ad_thumb = crypto::build_chunk_ad(make_tag(crypto::ChunkDomain::Thumb));
    const auto ad_video = crypto::build_chunk_ad(make_tag(crypto::ChunkDomain::Video));

    std::vector<uint8_t> s_data, s_thumb;
    REQUIRE(crypto::seal(key.as_span(), nonce, plain, s_data, ad_data));
    REQUIRE(crypto::seal(key.as_span(), nonce, plain, s_thumb, ad_thumb));
    // Tags differ across domains even with identical key/nonce/plaintext.
    CHECK_FALSE(std::equal(s_data.end() - crypto::TAG_SIZE, s_data.end(),
                           s_thumb.end() - crypto::TAG_SIZE));

    std::vector<uint8_t> ok;
    REQUIRE(crypto::open(key.as_span(), nonce, s_data, ok, ad_data));       // own AD opens
    CHECK_BYTES_EQ(std::span<const uint8_t>(ok), std::span<const uint8_t>(plain));
    std::vector<uint8_t> bad;
    CHECK_FALSE(crypto::open(key.as_span(), nonce, s_data, bad, ad_thumb));  // other AD fails
    CHECK_FALSE(crypto::open(key.as_span(), nonce, s_data, bad, ad_video));
}

// A wrong owner, record, or sequence in the AD must fail authentication.
TEST(context_ad_binds_every_identity_field)
{
    auto key = random_key();
    std::array<uint8_t, crypto::NONCE_SIZE> nonce;
    (void)crypto::fill_random(nonce);
    std::array<uint8_t, 16> plain;
    plain.fill(0x99);

    crypto::ChunkTag t;
    t.domain = crypto::ChunkDomain::Data;
    t.context_bound = true;
    for (size_t i = 0; i < t.owner.size(); ++i)  t.owner[i]  = 1;
    for (size_t i = 0; i < t.record.size(); ++i) t.record[i] = 2;

    const auto good_ad = crypto::build_chunk_ad(t);
    std::vector<uint8_t> sealed;
    REQUIRE(crypto::seal(key.as_span(), nonce, plain, sealed, good_ad));

    // Mutate one identity field at a time; each must fail to open.
    {
        auto t2 = t; ++t2.owner[0];
        std::vector<uint8_t> out;
        CHECK_FALSE(crypto::open(key.as_span(), nonce, sealed, out, crypto::build_chunk_ad(t2)));
    }
    {
        auto t2 = t; ++t2.record[0];
        std::vector<uint8_t> out;
        CHECK_FALSE(crypto::open(key.as_span(), nonce, sealed, out, crypto::build_chunk_ad(t2)));
    }
    {
        auto t2 = t; ++t2.sequence;
        std::vector<uint8_t> out;
        CHECK_FALSE(crypto::open(key.as_span(), nonce, sealed, out, crypto::build_chunk_ad(t2)));
    }
    {
        auto t2 = t; t2.domain = crypto::ChunkDomain::Thumb;
        std::vector<uint8_t> out;
        CHECK_FALSE(crypto::open(key.as_span(), nonce, sealed, out, crypto::build_chunk_ad(t2)));
    }
    // And the correct AD still opens it.
    std::vector<uint8_t> ok;
    REQUIRE(crypto::open(key.as_span(), nonce, sealed, ok, good_ad));
    CHECK_BYTES_EQ(std::span<const uint8_t>(ok), std::span<const uint8_t>(plain));
}
