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

// Internal linkage: several test files each define their own `TempVault`
// with a DIFFERENT layout. At namespace scope those are one-definition-rule
// violations — the member functions are implicitly inline, so the linker keeps
// a single copy and silently discards the rest.
namespace {

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

}  // namespace

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

static bool write_header(const std::string& path, const vault::Header& h)
{
    std::array<uint8_t, vault::HEADER_SIZE> raw{};
    h.serialize(raw);
    std::FILE* fp = std::fopen(path.c_str(), "r+b");
    if (!fp) return false;
    const bool ok = std::fwrite(raw.data(), 1, raw.size(), fp) == raw.size() &&
                    std::fflush(fp) == 0;
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

TEST(change_password_migrates_legacy_kdf_encoding)
{
    TempVault tv("legacy_kdf");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("ab"), bytes("c"), kTestKdf, v)
                == vault::VaultResult::Ok);
    }

    // Convert the wrap to the original password||keyfile encoding, simulating
    // an existing pre-v2 vault without touching its encrypted index/data.
    vault::Header h;
    REQUIRE(read_header(tv.str(), h));
    crypto::SecureBuffer<crypto::KEY_SIZE> v2_kek;
    crypto::SecureBuffer<crypto::KEY_SIZE> legacy_kek;
    crypto::SecureBuffer<crypto::KEY_SIZE> master;
    REQUIRE(crypto::derive_key(bytes("ab"), bytes("c"), h.salt, h.kdf, v2_kek));
    std::array<uint8_t, crypto::KEY_SIZE + crypto::TAG_SIZE> sealed{};
    std::memcpy(sealed.data(), h.wrapped_master_key.data(), crypto::KEY_SIZE);
    std::memcpy(sealed.data() + crypto::KEY_SIZE, h.mk_tag.data(), crypto::TAG_SIZE);
    // Phase 99: a fresh vault's master-key wrap is bound to the vault_id.
    crypto::ChunkTag mk_tag2;
    mk_tag2.domain        = crypto::ChunkDomain::MkWrap;
    mk_tag2.owner         = h.vault_id;
    mk_tag2.context_bound = true;
    const auto mk_ad = crypto::build_chunk_ad(mk_tag2);
    REQUIRE(crypto::open_to(v2_kek.as_span(), h.mk_nonce, sealed, master.span(), mk_ad));
    REQUIRE(crypto::derive_key(bytes("ab"), bytes("c"), h.salt, h.kdf, legacy_kek,
                               crypto::KdfInputFormat::LegacyConcat));
    std::vector<uint8_t> legacy_wrap;
    // The legacy-KEK wrap still rides the SAME Phase 99 MkWrap AD — only the
    // KDF input encoding is legacy here; the vault's AEAD context flag stays
    // set (its index blob is already context-bound and must keep opening).
    REQUIRE(crypto::seal(legacy_kek.as_span(), h.mk_nonce, master.as_span(), legacy_wrap, mk_ad));
    std::memcpy(h.wrapped_master_key.data(), legacy_wrap.data(), crypto::KEY_SIZE);
    std::memcpy(h.mk_tag.data(), legacy_wrap.data() + crypto::KEY_SIZE, crypto::TAG_SIZE);
    h.flags &= ~vault::FLAG_DOMAIN_SEPARATED_KDF;
    REQUIRE(write_header(tv.str(), h));

    vault::Vault legacy;
    REQUIRE(vault::Vault::open(tv.str(), legacy) == vault::VaultResult::Ok);
    REQUIRE(legacy.unlock(bytes("a"), bytes("bc")) == vault::VaultResult::Ok);
    REQUIRE(legacy.change_password(bytes("ab"), bytes("c"), bytes("new"), {})
            == vault::VaultResult::Ok);
    REQUIRE(read_header(tv.str(), h));
    CHECK(vault::domain_separated_kdf(h));

    vault::Vault migrated;
    REQUIRE(vault::Vault::open(tv.str(), migrated) == vault::VaultResult::Ok);
    CHECK_EQ(migrated.unlock(bytes("new"), {}), vault::VaultResult::Ok);
}
