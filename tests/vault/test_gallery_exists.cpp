// Phase 78: vault::gallery_exists — check if a gallery path exists
// (for DualGalleryScreen walk-up on vault changes).

#include "test_framework.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vault/vault.h"

namespace fs = std::filesystem;

using vault::Vault;
using vault::VaultResult;

// --- helpers ---------------------------------------------------------------

static const crypto::KdfParams kKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

static std::vector<uint8_t> blob(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 13 + seed);
    return v;
}

// Internal linkage: each vault test file defines its own `TempVault` with a
// different layout. At namespace scope those are one-definition-rule
// violations — the member functions are implicitly inline, so the linker keeps
// a single copy and silently discards the rest.
namespace {

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_gallery_exists_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    std::string str() const { return path.string(); }
};

}  // namespace

// --- tests ---------------------------------------------------------------

TEST(gallery_exists_root_always_exists_on_unlocked_vault)
{
    TempVault tv("root");
    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == VaultResult::Ok);
    CHECK_TRUE(vault::gallery_exists(v, ""));
}

TEST(gallery_exists_created_gallery_exists)
{
    TempVault tv("created");
    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == VaultResult::Ok);
    REQUIRE(v.create_gallery("photos") == VaultResult::Ok);
    CHECK_TRUE(vault::gallery_exists(v, "photos"));
}

TEST(gallery_exists_nested_gallery_path_exists)
{
    TempVault tv("nested");
    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == VaultResult::Ok);
    REQUIRE(v.create_gallery("a/b") == VaultResult::Ok);
    CHECK_TRUE(vault::gallery_exists(v, "a"));
    CHECK_TRUE(vault::gallery_exists(v, "a/b"));
}

TEST(gallery_exists_non_existent_path_returns_false)
{
    TempVault tv("missing");
    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == VaultResult::Ok);
    CHECK_FALSE(vault::gallery_exists(v, "nope"));
    CHECK_FALSE(vault::gallery_exists(v, "a/b/c"));
}

TEST(gallery_exists_media_file_path_returns_false)
{
    TempVault tv("image");
    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == VaultResult::Ok);
    REQUIRE(v.add_image("", blob(500, 42), "photo.jpg") == VaultResult::Ok);
    CHECK_FALSE(vault::gallery_exists(v, "photo.jpg"));
}

TEST(gallery_exists_locked_vault_returns_false)
{
    TempVault tv("locked");

    {
        Vault v;
        REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == VaultResult::Ok);
        REQUIRE(v.create_gallery("g") == VaultResult::Ok);
        v.lock();
    }

    Vault v2;
    REQUIRE(Vault::open(tv.str(), v2) == VaultResult::Ok);  // opens LOCKED
    CHECK_FALSE(vault::gallery_exists(v2, ""));
    CHECK_FALSE(vault::gallery_exists(v2, "g"));
}
