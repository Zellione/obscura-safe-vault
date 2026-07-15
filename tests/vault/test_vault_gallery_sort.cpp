// Phase 37: Vault-level persisted per-gallery sort order — set_gallery_sort /
// gallery_sort_key, and Vault::list applying the stored key (folders-first,
// natural-order names, etc.) for every caller (grid/list/viewer/slideshow all
// funnel through list()).

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

// --- helpers --------------------------------------------------------------

static const crypto::KdfParams kSortKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

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
               ("osv_sort_test_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
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

static std::vector<std::string> names(const std::vector<const vault::IndexNode*>& v)
{
    std::vector<std::string> out;
    out.reserve(v.size());
    for (const auto* n : v) out.push_back(n->name);
    return out;
}

// --- tests ----------------------------------------------------------------

TEST(gallery_sort_defaults_to_manual_on_a_fresh_gallery)
{
    TempVault tv("default");

    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kSortKdf, v) == VaultResult::Ok);
    REQUIRE(v.create_gallery("trip") == VaultResult::Ok);

    CHECK_TRUE(vault::gallery_sort_key(v, "") == SortKey::Manual);
    CHECK_TRUE(vault::gallery_sort_key(v, "trip") == SortKey::Manual);
}

TEST(gallery_sort_set_persists_across_lock_reopen)
{
    TempVault tv("persist");
    auto img = pattern(2000, 3);

    {
        Vault v;
        REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kSortKdf, v) == VaultResult::Ok);
        REQUIRE(v.add_image("", img, "image1.jpg") == VaultResult::Ok);
        REQUIRE(v.add_image("", img, "image10.jpg") == VaultResult::Ok);
        REQUIRE(v.add_image("", img, "image2.jpg") == VaultResult::Ok);

        REQUIRE(vault::set_gallery_sort(v, "", SortKey::NameAsc) == VaultResult::Ok);
        CHECK_TRUE(vault::gallery_sort_key(v, "") == SortKey::NameAsc);
        v.lock();
    }

    Vault v2;
    REQUIRE(Vault::open(tv.str(), v2) == VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == VaultResult::Ok);

    CHECK_TRUE(vault::gallery_sort_key(v2, "") == SortKey::NameAsc);
    // Natural order, not lexicographic: image1 < image2 < image10.
    CHECK(names(v2.list("")) ==
          std::vector<std::string>({"image1.jpg", "image2.jpg", "image10.jpg"}));
}

TEST(gallery_sort_list_applies_the_stored_key_to_sub_galleries)
{
    // A gallery holds either sub-galleries or images, never both (project
    // invariant), so this exercises NameAsc/NameDesc over a parent of only
    // sub-galleries — the other real-world shape gallery_sort has to handle.
    TempVault tv("apply");

    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kSortKdf, v) == VaultResult::Ok);
    REQUIRE(v.create_gallery("z_gallery") == VaultResult::Ok);
    REQUIRE(v.create_gallery("a_gallery") == VaultResult::Ok);
    REQUIRE(v.create_gallery("m_gallery") == VaultResult::Ok);

    REQUIRE(vault::set_gallery_sort(v, "", SortKey::NameAsc) == VaultResult::Ok);
    CHECK(names(v.list("")) ==
          std::vector<std::string>({"a_gallery", "m_gallery", "z_gallery"}));

    REQUIRE(vault::set_gallery_sort(v, "", SortKey::NameDesc) == VaultResult::Ok);
    CHECK(names(v.list("")) ==
          std::vector<std::string>({"z_gallery", "m_gallery", "a_gallery"}));
}

TEST(gallery_sort_other_galleries_are_unaffected)
{
    TempVault tv("scoped");
    auto img = pattern(1200, 9);

    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kSortKdf, v) == VaultResult::Ok);
    REQUIRE(v.create_gallery("sorted") == VaultResult::Ok);
    REQUIRE(v.create_gallery("untouched") == VaultResult::Ok);
    REQUIRE(v.add_image("sorted", img, "b.jpg") == VaultResult::Ok);
    REQUIRE(v.add_image("sorted", img, "a.jpg") == VaultResult::Ok);
    REQUIRE(v.add_image("untouched", img, "b.jpg") == VaultResult::Ok);
    REQUIRE(v.add_image("untouched", img, "a.jpg") == VaultResult::Ok);

    REQUIRE(vault::set_gallery_sort(v, "sorted", SortKey::NameAsc) == VaultResult::Ok);

    CHECK(names(v.list("sorted")) == std::vector<std::string>({"a.jpg", "b.jpg"}));
    // "untouched" keeps raw insertion order (Manual, the default).
    CHECK(names(v.list("untouched")) == std::vector<std::string>({"b.jpg", "a.jpg"}));
}

TEST(gallery_sort_set_unchanged_key_is_a_noop_ok)
{
    TempVault tv("noop");

    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kSortKdf, v) == VaultResult::Ok);
    REQUIRE(v.create_gallery("g") == VaultResult::Ok);

    CHECK_TRUE(vault::set_gallery_sort(v, "g", SortKey::Manual) == VaultResult::Ok);  // already Manual
}

TEST(gallery_sort_set_on_missing_path_returns_not_found)
{
    TempVault tv("missing");

    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kSortKdf, v) == VaultResult::Ok);
    CHECK_EQ(vault::set_gallery_sort(v, "nope", SortKey::NameAsc), VaultResult::NotFound);
    CHECK_TRUE(vault::gallery_sort_key(v, "nope") == SortKey::Manual);  // missing path -> Manual, not a crash
}

TEST(gallery_sort_set_on_locked_vault_fails)
{
    TempVault tv("locked");

    {
        Vault v;
        REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kSortKdf, v) == VaultResult::Ok);
        REQUIRE(v.create_gallery("g") == VaultResult::Ok);
        v.lock();
    }

    Vault v2;
    REQUIRE(Vault::open(tv.str(), v2) == VaultResult::Ok);  // opens LOCKED
    CHECK_EQ(vault::set_gallery_sort(v2, "g", SortKey::NameAsc), VaultResult::Locked);
}

TEST(gallery_sort_size_key_reflects_stored_orig_size)
{
    // orig_size is exact (unlike created_ts, which is Unix-seconds and can tie
    // within one test run), so this exercises the real Vault::add_image ->
    // IndexNode::meta.orig_size -> gallery_sort plumbing deterministically.
    TempVault tv("size");

    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kSortKdf, v) == VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(3000, 1), "big.jpg") == VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(1000, 2), "small.jpg") == VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(2000, 3), "mid.jpg") == VaultResult::Ok);

    REQUIRE(vault::set_gallery_sort(v, "", SortKey::SizeAsc) == VaultResult::Ok);
    CHECK(names(v.list("")) ==
          std::vector<std::string>({"small.jpg", "mid.jpg", "big.jpg"}));

    REQUIRE(vault::set_gallery_sort(v, "", SortKey::SizeDesc) == VaultResult::Ok);
    CHECK(names(v.list("")) ==
          std::vector<std::string>({"big.jpg", "mid.jpg", "small.jpg"}));
}
