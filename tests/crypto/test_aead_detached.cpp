#include "test_framework.h"

#include <array>
#include <vector>

#include "crypto/aead.h"
#include "crypto/random.h"
#include "crypto/secure_mem.h"

// Detached AEAD (crypto::seal / crypto::open) takes an explicit caller-supplied
// nonce and produces ciphertext||tag with NO nonce prefix. The vault stores the
// nonce separately (header master-key wrap + double-buffered index slots), so
// these are the primitives the vault layer builds on.

static crypto::SecureBuffer<crypto::KEY_SIZE> random_key()
{
    crypto::SecureBuffer<crypto::KEY_SIZE> k;
    (void)crypto::fill_random(k.span());
    return k;
}

TEST(seal_open_roundtrip)
{
    auto key = random_key();
    std::array<uint8_t, crypto::NONCE_SIZE> nonce{};
    REQUIRE(crypto::fill_random(nonce));

    const std::array<size_t, 4> sizes = {0, 1, 32, 4096};
    for (size_t n : sizes) {
        std::vector<uint8_t> plain(n);
        for (size_t i = 0; i < n; ++i) plain[i] = static_cast<uint8_t>(i * 13 + 5);

        std::vector<uint8_t> sealed;
        crypto::seal(key.as_span(), nonce, plain, sealed);
        // No nonce prefix: output is exactly ciphertext + tag.
        CHECK_EQ(sealed.size(), n + crypto::TAG_SIZE);

        std::vector<uint8_t> recovered;
        REQUIRE(crypto::open(key.as_span(), nonce, sealed, recovered));
        CHECK_BYTES_EQ(std::span<const uint8_t>(recovered),
                       std::span<const uint8_t>(plain));
    }
}

TEST(seal_is_deterministic_for_fixed_nonce)
{
    auto key = random_key();
    std::array<uint8_t, crypto::NONCE_SIZE> nonce{};
    REQUIRE(crypto::fill_random(nonce));
    std::array<uint8_t, 16> plain; plain.fill(0x5A);

    std::vector<uint8_t> a, b;
    crypto::seal(key.as_span(), nonce, plain, a);
    crypto::seal(key.as_span(), nonce, plain, b);
    // Same key+nonce+plaintext => identical output (no internal randomness).
    CHECK_BYTES_EQ(std::span<const uint8_t>(a), std::span<const uint8_t>(b));
}

TEST(open_detects_tamper)
{
    auto key = random_key();
    std::array<uint8_t, crypto::NONCE_SIZE> nonce{};
    REQUIRE(crypto::fill_random(nonce));
    std::array<uint8_t, 24> plain; plain.fill(0x11);

    std::vector<uint8_t> sealed;
    crypto::seal(key.as_span(), nonce, plain, sealed);

    for (size_t pos = 0; pos < sealed.size(); pos += 7) {
        std::vector<uint8_t> bad = sealed;
        bad[pos] ^= 0x01;
        std::vector<uint8_t> out;
        CHECK_FALSE(crypto::open(key.as_span(), nonce, bad, out));
        CHECK_TRUE(out.empty());
    }
}

TEST(open_with_wrong_nonce_fails)
{
    auto key = random_key();
    std::array<uint8_t, crypto::NONCE_SIZE> nonce{};
    REQUIRE(crypto::fill_random(nonce));
    std::array<uint8_t, 8> plain; plain.fill(0x77);

    std::vector<uint8_t> sealed;
    crypto::seal(key.as_span(), nonce, plain, sealed);

    std::array<uint8_t, crypto::NONCE_SIZE> wrong = nonce;
    wrong[0] ^= 0x01;
    std::vector<uint8_t> out;
    CHECK_FALSE(crypto::open(key.as_span(), wrong, sealed, out));
}

TEST(open_rejects_short_input)
{
    auto key = random_key();
    std::array<uint8_t, crypto::NONCE_SIZE> nonce{};
    std::vector<uint8_t> too_short(crypto::TAG_SIZE - 1, 0);
    std::vector<uint8_t> out;
    CHECK_FALSE(crypto::open(key.as_span(), nonce, too_short, out));
}

TEST(seal_authenticates_associated_data)
{
    auto key = random_key();
    std::array<uint8_t, crypto::NONCE_SIZE> nonce{};
    REQUIRE(crypto::fill_random(nonce));
    std::array<uint8_t, 4> plain = {1, 2, 3, 4};
    std::array<uint8_t, 3> ad    = {9, 8, 7};

    std::vector<uint8_t> sealed;
    crypto::seal(key.as_span(), nonce, plain, sealed, ad);

    std::vector<uint8_t> out;
    REQUIRE(crypto::open(key.as_span(), nonce, sealed, out, ad));
    CHECK_BYTES_EQ(std::span<const uint8_t>(out), std::span<const uint8_t>(plain));

    std::array<uint8_t, 3> wrong_ad = {9, 8, 6};
    CHECK_FALSE(crypto::open(key.as_span(), nonce, sealed, out, wrong_ad));
}
