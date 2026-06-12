#include "test_framework.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vault/header.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

// Phase 7: password change. change_password() re-wraps the master key under a
// KEK derived from the new credentials (fresh salt + nonce). Data chunks are
// never re-encrypted — the master key itself does not change.

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
               ("osv_ptest_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

static bool read_file_range(const std::string& path, uint64_t off,
                            std::vector<uint8_t>& out, size_t len)
{
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    out.assign(len, 0);
    const bool ok = std::fseek(fp, static_cast<long>(off), SEEK_SET) == 0 &&
                    std::fread(out.data(), 1, len, fp) == len;
    std::fclose(fp);
    return ok;
}

static bool read_header(const std::string& path, vault::Header& out)
{
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    std::array<uint8_t, vault::HEADER_SIZE> raw{};
    const bool ok = std::fread(raw.data(), 1, raw.size(), fp) == raw.size() &&
                    vault::Header::parse(raw, out);
    std::fclose(fp);
    return ok;
}

TEST(change_password_old_rejected_new_unlocks)
{
    TempVault tv("change");
    const auto img = pattern(10000, 1);
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("old pass"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", img, "pic.bin") == vault::VaultResult::Ok);
        REQUIRE(v.change_password(bytes("old pass"), {}, bytes("new pass"), {})
                == vault::VaultResult::Ok);
    }

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    CHECK_EQ(v2.unlock(bytes("old pass"), {}), vault::VaultResult::AuthFailed);
    REQUIRE(v2.unlock(bytes("new pass"), {}) == vault::VaultResult::Ok);

    auto kids = v2.list("");
    REQUIRE(kids.size() == 1);
    crypto::SecureBytes out;
    REQUIRE(v2.read_image(*kids[0], out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(img));
}

TEST(change_password_does_not_touch_data_chunks)
{
    TempVault tv("norewrite");
    uint64_t data_off = 0, data_len = 0;

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw1"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(20000, 2), "pic.bin") == vault::VaultResult::Ok);
    {
        auto kids = v.list("");
        REQUIRE(kids.size() == 1);
        data_off = kids[0]->meta.data_offset;
        data_len = kids[0]->meta.data_length;
    }

    vault::Header before_h;
    std::vector<uint8_t> before_chunk;
    REQUIRE(read_header(tv.str(), before_h));
    REQUIRE(read_file_range(tv.str(), data_off, before_chunk, data_len));

    REQUIRE(v.change_password(bytes("pw1"), {}, bytes("pw2"), {})
            == vault::VaultResult::Ok);

    // The encrypted data chunk is byte-identical: only the header wrap changed.
    std::vector<uint8_t> after_chunk;
    REQUIRE(read_file_range(tv.str(), data_off, after_chunk, data_len));
    CHECK_BYTES_EQ(std::span<const uint8_t>(after_chunk),
                   std::span<const uint8_t>(before_chunk));

    // Fresh salt + nonce: a re-wrap must never reuse the old ones.
    vault::Header after_h;
    REQUIRE(read_header(tv.str(), after_h));
    CHECK_FALSE(testing::bytes_equal(after_h.salt, before_h.salt));
    CHECK_FALSE(testing::bytes_equal(after_h.mk_nonce, before_h.mk_nonce));
}

TEST(change_password_wrong_old_credentials_rejected)
{
    TempVault tv("wrongold");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("right"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    CHECK_EQ(v.change_password(bytes("wrong"), {}, bytes("new"), {}),
             vault::VaultResult::AuthFailed);

    // The vault is untouched: the original password still works after reopen.
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    CHECK_EQ(v2.unlock(bytes("right"), {}), vault::VaultResult::Ok);
}

TEST(change_password_can_add_and_remove_keyfile)
{
    TempVault tv("keyfile");
    const auto keyfile = pattern(64, 9);
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        // Add a keyfile requirement.
        REQUIRE(v.change_password(bytes("pw"), {}, bytes("pw"), keyfile)
                == vault::VaultResult::Ok);
    }
    {
        vault::Header h;
        REQUIRE(read_header(tv.str(), h));
        CHECK_EQ(h.keyfile_required, 1);

        vault::Vault v;
        REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
        CHECK_EQ(v.unlock(bytes("pw"), {}), vault::VaultResult::AuthFailed);
        REQUIRE(v.unlock(bytes("pw"), keyfile) == vault::VaultResult::Ok);

        // Drop the keyfile again (change_password also works while unlocked).
        REQUIRE(v.change_password(bytes("pw"), keyfile, bytes("pw"), {})
                == vault::VaultResult::Ok);
        CHECK_TRUE(v.is_unlocked());  // state preserved across a re-wrap
    }
    vault::Header h;
    REQUIRE(read_header(tv.str(), h));
    CHECK_EQ(h.keyfile_required, 0);

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    CHECK_EQ(v2.unlock(bytes("pw"), {}), vault::VaultResult::Ok);
}
