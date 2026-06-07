#include "test_framework.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vault/vault.h"

namespace fs = std::filesystem;

// --- helpers --------------------------------------------------------------

// Cheap Argon2 params so the test suite stays fast (real vaults use the
// 64 MiB / 3-pass default). Security of the KDF is covered by the crypto tests.
static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

// RAII temp path: a unique .osv file removed when the helper goes out of scope.
struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_test_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
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

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

// --- tests ----------------------------------------------------------------

TEST(vault_create_leaves_vault_unlocked)
{
    TempVault tv("create");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("hunter2"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    CHECK_TRUE(v.is_unlocked());
}

TEST(vault_add_and_read_image_same_session)
{
    TempVault tv("addread");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    auto img = pattern(20000, 11);
    REQUIRE(v.add_image("", img, "cat.jpg") == vault::VaultResult::Ok);

    auto children = v.list("");
    REQUIRE(children.size() == 1);
    REQUIRE(children[0]->is_image());

    crypto::SecureBytes out;
    REQUIRE(v.read_image(*children[0], out) == vault::VaultResult::Ok);
    REQUIRE(out.size() == img.size());
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(img));
}

TEST(vault_roundtrip_across_lock_reopen_unlock)
{
    TempVault tv("reopen");
    auto img = pattern(12345, 22);
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("correct horse"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", img, "photo.png") == vault::VaultResult::Ok);
        v.lock();
        CHECK_FALSE(v.is_unlocked());
    }

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    CHECK_FALSE(v2.is_unlocked());
    REQUIRE(v2.unlock(bytes("correct horse"), {}) == vault::VaultResult::Ok);
    CHECK_TRUE(v2.is_unlocked());

    auto children = v2.list("");
    REQUIRE(children.size() == 1);
    crypto::SecureBytes out;
    REQUIRE(v2.read_image(*children[0], out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(img));
}

TEST(vault_unlock_with_wrong_password_fails)
{
    TempVault tv("wrongpw");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("rightpw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", pattern(100, 1), "a.jpg") == vault::VaultResult::Ok);
    }
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    CHECK_EQ(v2.unlock(bytes("wrongpw"), {}), vault::VaultResult::AuthFailed);
    CHECK_FALSE(v2.is_unlocked());
}

TEST(vault_keyfile_required_to_unlock)
{
    TempVault tv("keyfile");
    auto keyfile = pattern(64, 99);
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), keyfile, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", pattern(50, 2), "x.jpg") == vault::VaultResult::Ok);
    }
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    // Right password but missing keyfile -> auth fails.
    CHECK_EQ(v2.unlock(bytes("pw"), {}), vault::VaultResult::AuthFailed);
    // Right password + keyfile -> success.
    REQUIRE(v2.unlock(bytes("pw"), keyfile) == vault::VaultResult::Ok);
}

TEST(vault_nested_galleries_and_list)
{
    TempVault tv("nested");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    REQUIRE(v.create_gallery("vacation") == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("vacation/2024") == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("vacation/2025") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("vacation/2024", pattern(80, 4), "beach.jpg")
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("vacation/2024", pattern(90, 5), "sunset.jpg")
            == vault::VaultResult::Ok);

    auto root_children = v.list("");
    REQUIRE(root_children.size() == 1);
    CHECK_EQ(root_children[0]->name, std::string("vacation"));
    CHECK_TRUE(root_children[0]->is_gallery());

    auto vac = v.list("vacation");
    REQUIRE(vac.size() == 2);

    auto y2024 = v.list("vacation/2024");
    REQUIRE(y2024.size() == 2);
    CHECK_TRUE(y2024[0]->is_image());
    CHECK_TRUE(y2024[1]->is_image());
}

TEST(vault_enforces_leaf_invariant)
{
    TempVault tv("leaf");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    // A gallery holding a sub-gallery cannot also hold images.
    REQUIRE(v.create_gallery("mixed/sub") == vault::VaultResult::Ok);
    CHECK_EQ(v.add_image("mixed", pattern(10, 1), "nope.jpg"),
             vault::VaultResult::InvalidArg);

    // A gallery holding images cannot gain a sub-gallery.
    REQUIRE(v.create_gallery("photos") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("photos", pattern(10, 1), "img.jpg") == vault::VaultResult::Ok);
    CHECK_EQ(v.create_gallery("photos/sub"), vault::VaultResult::InvalidArg);
}

TEST(vault_remove_image)
{
    TempVault tv("remove");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(30, 1), "a.jpg") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(40, 2), "b.jpg") == vault::VaultResult::Ok);
    REQUIRE(v.list("").size() == 2);

    REQUIRE(v.remove_image("", "a.jpg") == vault::VaultResult::Ok);
    auto remaining = v.list("");
    REQUIRE(remaining.size() == 1);
    CHECK_EQ(remaining[0]->name, std::string("b.jpg"));

    CHECK_EQ(v.remove_image("", "ghost.jpg"), vault::VaultResult::NotFound);
}

TEST(vault_operations_require_unlock)
{
    TempVault tv("locked");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
    }
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    // Still locked: mutating ops are rejected.
    CHECK_EQ(v2.add_image("", pattern(10, 1), "a.jpg"), vault::VaultResult::Locked);
    CHECK_EQ(v2.create_gallery("g"), vault::VaultResult::Locked);
}
