// Phase 49: vault-global settings — held, exposed, persisted through the
// crash-safe index swap, and seeded for a vault that has never stored any.

#include "test_framework.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vault/vault.h"

namespace fs = std::filesystem;

using vault::SortKey;
using vault::Vault;
using vault::VaultResult;
using vault::VaultSettings;

// --- helpers --------------------------------------------------------------

static const crypto::KdfParams kSettingsKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

// Internal linkage: several vault test files each define their own `TempVault`
// with a DIFFERENT layout. At namespace scope those are one-definition-rule
// violations — the member functions are implicitly inline, so the linker keeps
// a single `~TempVault` and silently discards the rest. Whichever one wins gets
// called for every file's object, and this file's `Vault v` member then never
// gets destroyed (that leaked the whole Vault under ASan). An anonymous
// namespace makes the type distinct per translation unit.
namespace {

struct TempVault {
    fs::path path;
    Vault    v;

    explicit TempVault()
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_settings_test_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }

    ~TempVault()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }

    [[nodiscard]] std::string str() const { return path.string(); }

    // Create and unlock a vault for testing
    VaultResult create_and_unlock()
    {
        auto res = Vault::create(str(), bytes("testpw"), {}, kSettingsKdf, v);
        return res;
    }

    // Lock and then unlock the vault (simulates a reopen cycle)
    VaultResult relock_and_unlock()
    {
        v.lock();
        auto res = Vault::open(str(), v);
        if (res != VaultResult::Ok) {
            return res;
        }
        return v.unlock(bytes("testpw"), {});
    }
};

}  // namespace

// --- tests ----------------------------------------------------------------

TEST(vault_settings_start_seeded)
{
    TempVault tv;
    REQUIRE(tv.create_and_unlock() == VaultResult::Ok);

    const auto& s = vault::vault_settings(tv.v);
    CHECK(s.default_sort == SortKey::Insertion);
    CHECK(s.tiles_show_tags);
    CHECK_EQ(s.categories.size(), 8);
}

TEST(vault_settings_persist_across_lock_unlock)
{
    TempVault tv;
    REQUIRE(tv.create_and_unlock() == VaultResult::Ok);

    VaultSettings s = vault::vault_settings(tv.v);
    s.default_sort = SortKey::NameAsc;
    s.categories.push_back({.name = "studio", .swatch = 11});
    CHECK(vault::set_vault_settings(tv.v, s) == VaultResult::Ok);

    REQUIRE(tv.relock_and_unlock() == VaultResult::Ok);
    const auto& got = vault::vault_settings(tv.v);
    CHECK(got.default_sort == SortKey::NameAsc);
    REQUIRE(got.categories.size() == 9);
    CHECK(got.categories.back().name == "studio");
}

TEST(vault_settings_rejected_while_locked)
{
    TempVault tv;
    REQUIRE(tv.create_and_unlock() == VaultResult::Ok);
    tv.v.lock();
    CHECK(vault::set_vault_settings(tv.v, VaultSettings{}) == VaultResult::Locked);
}

TEST(vault_settings_survive_compaction)
{
    TempVault tv;
    REQUIRE(tv.create_and_unlock() == VaultResult::Ok);

    VaultSettings s = vault::vault_settings(tv.v);
    s.default_sort = SortKey::DateDesc;
    s.categories.push_back({.name = "studio", .swatch = 11});
    CHECK(vault::set_vault_settings(tv.v, s) == VaultResult::Ok);

    CHECK(tv.v.compact() == VaultResult::Ok);

    REQUIRE(tv.relock_and_unlock() == VaultResult::Ok);
    const auto& got = vault::vault_settings(tv.v);
    CHECK(got.default_sort == SortKey::DateDesc);
    REQUIRE(!got.categories.empty());
    CHECK(got.categories.back().name == "studio");
}

TEST(vault_settings_default_sort_orders_untouched_galleries)
{
    TempVault tv;
    REQUIRE(tv.create_and_unlock() == VaultResult::Ok);
    REQUIRE(tv.v.create_gallery("b") == VaultResult::Ok);
    REQUIRE(tv.v.create_gallery("a") == VaultResult::Ok);

    // Insertion default: creation order.
    auto kids = tv.v.list("");
    REQUIRE(kids.size() == 2);
    CHECK(kids[0]->name == "b");

    VaultSettings s = vault::vault_settings(tv.v);
    s.default_sort = SortKey::NameAsc;
    REQUIRE(vault::set_vault_settings(tv.v, s) == VaultResult::Ok);

    kids = tv.v.list("");
    REQUIRE(kids.size() == 2);
    CHECK(kids[0]->name == "a");        // the vault default now applies
}

TEST(vault_settings_gallery_override_beats_default)
{
    TempVault tv;
    REQUIRE(tv.create_and_unlock() == VaultResult::Ok);
    REQUIRE(tv.v.create_gallery("b") == VaultResult::Ok);
    REQUIRE(tv.v.create_gallery("a") == VaultResult::Ok);

    VaultSettings s = vault::vault_settings(tv.v);
    s.default_sort = SortKey::NameAsc;
    REQUIRE(vault::set_vault_settings(tv.v, s) == VaultResult::Ok);
    REQUIRE(vault::set_gallery_sort(tv.v, "", SortKey::Insertion) == VaultResult::Ok);

    const auto kids = tv.v.list("");
    REQUIRE(kids.size() == 2);
    CHECK(kids[0]->name == "b");        // pinned back to import order
}

TEST(vault_settings_survive_move)
{
    // Regression test: Vault's move constructor/assignment must properly
    // move settings_ with its categories vector and all allocations.
    TempVault tv;
    REQUIRE(tv.create_and_unlock() == VaultResult::Ok);

    VaultSettings original = vault::vault_settings(tv.v);
    REQUIRE(original.categories.size() == 8);  // seeded default
    original.default_sort = SortKey::DateAsc;
    REQUIRE(vault::set_vault_settings(tv.v, original) == VaultResult::Ok);

    // Move the vault to a new variable
    Vault moved = std::move(tv.v);
    REQUIRE(moved.is_unlocked());

    // Verify settings_ were properly moved
    const auto& moved_settings = vault::vault_settings(moved);
    CHECK(moved_settings.default_sort == SortKey::DateAsc);
    REQUIRE(moved_settings.categories.size() == 8);
    // Check that the categories are actual allocations (not dangling)
    CHECK(!moved_settings.categories.empty());
}
