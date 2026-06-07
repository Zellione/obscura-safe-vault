#include "test_framework.h"

#include <array>
#include <string_view>
#include <vector>

#include <monocypher.h>

#include "crypto/kdf.h"
#include "crypto/secure_mem.h"

// Argon2id known-answer test from RFC 9106 §5.3.
//   password = 32 x 0x01, salt = 16 x 0x02, secret = 8 x 0x03, ad = 12 x 0x04
//   t = 3, m = 32 KiB, p = 4, tag length = 32, version = 0x13
// Expected tag:
//   0d640df58d78766c08c037a34a8b53c9d01ef0452d75b65eb52520e96b01e659
//
// derive_key() intentionally does not expose the secret/ad inputs, so this KAT
// drives crypto_argon2 directly to lock down the underlying primitive.
TEST(argon2id_rfc9106_kat)
{
    std::array<uint8_t, 32> password; password.fill(0x01);
    std::array<uint8_t, 16> salt;     salt.fill(0x02);
    std::array<uint8_t, 8>  secret;   secret.fill(0x03);
    std::array<uint8_t, 12> ad;       ad.fill(0x04);

    static constexpr std::array<uint8_t, 32> expected = {{
        0x0d, 0x64, 0x0d, 0xf5, 0x8d, 0x78, 0x76, 0x6c,
        0x08, 0xc0, 0x37, 0xa3, 0x4a, 0x8b, 0x53, 0xc9,
        0xd0, 0x1e, 0xf0, 0x45, 0x2d, 0x75, 0xb6, 0x5e,
        0xb5, 0x25, 0x20, 0xe9, 0x6b, 0x01, 0xe6, 0x59,
    }};

    const uint32_t m = 32;  // KiB == nb_blocks
    std::vector<uint8_t> work(static_cast<size_t>(m) * 1024u);

    std::array<uint8_t, 32> out{};
    const crypto_argon2_config config{
        .algorithm = CRYPTO_ARGON2_ID, .nb_blocks = m, .nb_passes = 3, .nb_lanes = 4};
    const crypto_argon2_inputs inputs{
        .pass = password.data(), .salt = salt.data(),
        .pass_size = 32, .salt_size = 16};
    const crypto_argon2_extras extras{
        .key = secret.data(), .ad = ad.data(), .key_size = 8, .ad_size = 12};

    crypto_argon2(out.data(), 32, work.data(), config, inputs, extras);

    CHECK_BYTES_EQ(std::span<const uint8_t>(out),
                   std::span<const uint8_t>(expected));
}

// derive_key() is deterministic: same inputs -> same key; different salt -> diff.
TEST(derive_key_deterministic)
{
    constexpr std::string_view pw = "correct horse battery staple";
    std::span<const uint8_t> password(reinterpret_cast<const uint8_t*>(pw.data()),
                                      pw.size());
    std::span<const uint8_t> no_keyfile{};

    std::array<uint8_t, crypto::SALT_SIZE> salt_a; salt_a.fill(0xAB);
    std::array<uint8_t, crypto::SALT_SIZE> salt_b; salt_b.fill(0xCD);

    // Keep the test fast: tiny Argon2 params (correctness, not hardness, here).
    const crypto::KdfParams params{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

    crypto::SecureBuffer<crypto::KEY_SIZE> k1;
    crypto::SecureBuffer<crypto::KEY_SIZE> k2;
    crypto::SecureBuffer<crypto::KEY_SIZE> k3;
    REQUIRE(crypto::derive_key(password, no_keyfile,
                               std::span<const uint8_t, crypto::SALT_SIZE>(salt_a),
                               params, k1));
    REQUIRE(crypto::derive_key(password, no_keyfile,
                               std::span<const uint8_t, crypto::SALT_SIZE>(salt_a),
                               params, k2));
    REQUIRE(crypto::derive_key(password, no_keyfile,
                               std::span<const uint8_t, crypto::SALT_SIZE>(salt_b),
                               params, k3));

    CHECK_BYTES_EQ(k1.as_span(), k2.as_span());       // same salt -> identical
    CHECK_FALSE(::testing::bytes_equal(k1.as_span(), k3.as_span())); // diff salt
}

// A keyfile changes the derived key (password ‖ keyfile).
TEST(derive_key_keyfile_changes_output)
{
    constexpr std::string_view pw = "hunter2";
    std::span<const uint8_t> password(reinterpret_cast<const uint8_t*>(pw.data()),
                                      pw.size());
    std::array<uint8_t, 32> keyfile; keyfile.fill(0x5A);

    std::array<uint8_t, crypto::SALT_SIZE> salt; salt.fill(0x11);
    const crypto::KdfParams params{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

    crypto::SecureBuffer<crypto::KEY_SIZE> with;
    crypto::SecureBuffer<crypto::KEY_SIZE> without;
    REQUIRE(crypto::derive_key(password, keyfile,
                               std::span<const uint8_t, crypto::SALT_SIZE>(salt),
                               params, with));
    REQUIRE(crypto::derive_key(password, std::span<const uint8_t>{},
                               std::span<const uint8_t, crypto::SALT_SIZE>(salt),
                               params, without));

    CHECK_FALSE(::testing::bytes_equal(with.as_span(), without.as_span()));
}
