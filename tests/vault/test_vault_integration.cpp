#include "test_framework.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <monocypher.h>  // crypto_blake2b for content checksums

#include "vault/vault.h"

namespace fs = std::filesystem;

// These exercise the Phase 2 acceptance criteria end-to-end (ROADMAP):
//   * create -> add images -> lock -> reopen -> unlock -> read back, checksums match
//   * crash recovery: a truncated active index falls back to the previous index
//   * tamper detection: a flipped ciphertext byte yields an auth error, not data
//   * nested galleries survive a reopen

static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_itest_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = static_cast<uint8_t>((i * 131 + seed * 7 + (i >> 8)) & 0xFF);
    return v;
}

static std::array<uint8_t, 32> blake2b(std::span<const uint8_t> data)
{
    std::array<uint8_t, 32> h{};
    crypto_blake2b(h.data(), h.size(), data.data(), data.size());
    return h;
}

// Flip one byte at `pos` in the file at `path` (used to simulate at-rest tamper).
static bool flip_byte(const std::string& path, long pos)
{
    std::FILE* fp = std::fopen(path.c_str(), "r+b");
    if (!fp) return false;
    bool ok = std::fseek(fp, pos, SEEK_SET) == 0;
    int c = ok ? std::fgetc(fp) : EOF;
    ok = ok && c != EOF && std::fseek(fp, pos, SEEK_SET) == 0 &&
         std::fputc(c ^ 0x01, fp) != EOF;
    std::fclose(fp);
    return ok;
}

TEST(integration_three_images_checksums_match_after_reopen)
{
    TempVault tv("3img");

    std::vector<std::vector<uint8_t>> originals = {
        pattern(64 * 1024, 1), pattern(100000, 2), pattern(1, 3)};
    std::vector<std::array<uint8_t, 32>> sums;
    for (auto& o : originals) sums.push_back(blake2b(o));

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("strong passphrase"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", originals[0], "one.jpg")   == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", originals[1], "two.png")   == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", originals[2], "three.bmp") == vault::VaultResult::Ok);
        v.lock();
    }

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("strong passphrase"), {}) == vault::VaultResult::Ok);

    auto children = v2.list("");
    REQUIRE(children.size() == 3);
    for (size_t i = 0; i < children.size(); ++i) {
        crypto::SecureBytes out;
        REQUIRE(v2.read_image(*children[i], out) == vault::VaultResult::Ok);
        REQUIRE(out.size() == originals[i].size());
        auto got = blake2b(out.as_span());
        CHECK_BYTES_EQ(std::span<const uint8_t>(got), std::span<const uint8_t>(sums[i]));
    }
}

TEST(integration_crash_recovery_falls_back_to_previous_index)
{
    TempVault tv("crash");
    auto img1 = pattern(5000, 7);
    auto img2 = pattern(6000, 8);

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", img1, "first.jpg")  == vault::VaultResult::Ok);
        // Committing this writes a fresh index blob at end of file (the active one).
        REQUIRE(v.add_image("", img2, "second.jpg") == vault::VaultResult::Ok);
    }

    // Simulate a crash mid-swap: truncate the tail so the active slot's index
    // blob is unreadable. The previous slot (first.jpg only) must still load.
    std::error_code ec;
    const auto size = fs::file_size(tv.path, ec);
    REQUIRE(!ec);
    fs::resize_file(tv.path, size - 4, ec);
    REQUIRE(!ec);

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    auto children = v2.list("");
    REQUIRE(children.size() == 1);  // recovered the pre-crash index
    CHECK_EQ(children[0]->name, std::string("first.jpg"));

    crypto::SecureBytes out;
    REQUIRE(v2.read_image(*children[0], out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(img1));
}

TEST(integration_tampered_chunk_fails_authentication)
{
    TempVault tv("tamper");
    auto img = pattern(8000, 9);
    uint64_t data_offset = 0;

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", img, "secret.jpg") == vault::VaultResult::Ok);
        auto kids = v.list("");
        REQUIRE(kids.size() == 1);
        data_offset = kids[0]->meta.data_offset;
    }

    // Flip a byte inside the image's ciphertext (past its 24-byte nonce prefix).
    REQUIRE(flip_byte(tv.str(), static_cast<long>(data_offset) + crypto::NONCE_SIZE + 10));

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);  // index untouched

    auto kids = v2.list("");
    REQUIRE(kids.size() == 1);
    crypto::SecureBytes out;
    CHECK_EQ(v2.read_image(*kids[0], out), vault::VaultResult::AuthFailed);
    CHECK_TRUE(out.empty());
}

TEST(integration_nested_galleries_survive_reopen)
{
    TempVault tv("nest");
    auto img = pattern(2048, 4);

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("a/b/c") == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("a/b/d") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("a/b/c", img, "deep.jpg") == vault::VaultResult::Ok);
    }

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    REQUIRE(v2.list("").size() == 1);
    REQUIRE(v2.list("a").size() == 1);
    auto ab = v2.list("a/b");
    REQUIRE(ab.size() == 2);  // c and d
    auto abc = v2.list("a/b/c");
    REQUIRE(abc.size() == 1);
    CHECK_EQ(abc[0]->name, std::string("deep.jpg"));

    crypto::SecureBytes out;
    REQUIRE(v2.read_image(*abc[0], out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(img));
}
