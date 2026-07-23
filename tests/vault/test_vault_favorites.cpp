#include "test_framework.h"

#include <algorithm>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vault/vault.h"

namespace fs = std::filesystem;

using vault::Vault;
using vault::VaultResult;

// --- helpers --------------------------------------------------------------

// Cheap Argon2 params so tests stay fast.
static const crypto::KdfParams kFavKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

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
               ("osv_fav_test_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
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

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

// True if `hits` contains a hit whose path equals `path`.
static bool has_path(const std::vector<vault::SearchHit>& hits, std::string_view path)
{
    return std::ranges::any_of(hits, [&](const auto& h) { return h.path == path; });
}

// --- tests ----------------------------------------------------------------

TEST(favorites_toggle_on_image_persists_across_reopen)
{
    TempVault tv("img_persist");
    auto img = pattern(4000, 7);

    {
        Vault v;
        REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kFavKdf, v) == VaultResult::Ok);
        REQUIRE(v.add_image("", img, "photo.jpg") == VaultResult::Ok);

        REQUIRE(v.toggle_favorite("photo.jpg") == VaultResult::Ok);
        auto children = v.list("");
        REQUIRE(children.size() == 1);
        CHECK_TRUE(children[0]->favorite);

        v.lock();
    }

    Vault v2;
    REQUIRE(Vault::open(tv.str(), v2) == VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == VaultResult::Ok);
    auto children = v2.list("");
    REQUIRE(children.size() == 1);
    CHECK_TRUE(children[0]->favorite);
}

TEST(favorites_toggle_on_gallery_persists_across_reopen)
{
    TempVault tv("gal_persist");

    {
        Vault v;
        REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kFavKdf, v) == VaultResult::Ok);
        REQUIRE(v.create_gallery("trip") == VaultResult::Ok);

        REQUIRE(v.toggle_favorite("trip") == VaultResult::Ok);
        auto children = v.list("");
        REQUIRE(children.size() == 1);
        REQUIRE(children[0]->is_gallery());
        CHECK_TRUE(children[0]->favorite);

        v.lock();
    }

    Vault v2;
    REQUIRE(Vault::open(tv.str(), v2) == VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == VaultResult::Ok);
    auto children = v2.list("");
    REQUIRE(children.size() == 1);
    CHECK_TRUE(children[0]->favorite);
}

TEST(favorites_toggle_twice_returns_to_unfavorited)
{
    TempVault tv("flip");
    auto img = pattern(2000, 3);

    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kFavKdf, v) == VaultResult::Ok);
    REQUIRE(v.add_image("", img, "a.jpg") == VaultResult::Ok);

    REQUIRE(v.toggle_favorite("a.jpg") == VaultResult::Ok);
    CHECK_TRUE(v.list("")[0]->favorite);
    REQUIRE(v.toggle_favorite("a.jpg") == VaultResult::Ok);
    CHECK_FALSE(v.list("")[0]->favorite);
}

TEST(favorites_list_images_collects_across_whole_tree)
{
    TempVault tv("list_img");
    auto img = pattern(1500, 5);

    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kFavKdf, v) == VaultResult::Ok);
    REQUIRE(v.create_gallery("a/b") == VaultResult::Ok);
    REQUIRE(v.create_gallery("a/c") == VaultResult::Ok);
    REQUIRE(v.add_image("a/b", img, "x.jpg") == VaultResult::Ok);
    REQUIRE(v.add_image("a/b", img, "y.jpg") == VaultResult::Ok);
    REQUIRE(v.add_image("a/c", img, "z.jpg") == VaultResult::Ok);

    REQUIRE(v.toggle_favorite("a/b/x.jpg") == VaultResult::Ok);
    REQUIRE(v.toggle_favorite("a/c/z.jpg") == VaultResult::Ok);

    auto favs = v.list_favorite_images();
    REQUIRE(favs.size() == 2);
    CHECK_TRUE(has_path(favs, "a/b/x.jpg"));
    CHECK_TRUE(has_path(favs, "a/c/z.jpg"));
    CHECK_FALSE(has_path(favs, "a/b/y.jpg"));
    for (const auto& h : favs) {
        CHECK_FALSE(h.is_gallery);
        REQUIRE(h.node != nullptr);
        CHECK_TRUE(h.node->is_image());
    }
}

TEST(favorites_list_galleries_collects_favorited_galleries_only)
{
    TempVault tv("list_gal");

    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kFavKdf, v) == VaultResult::Ok);
    REQUIRE(v.create_gallery("a/b") == VaultResult::Ok);
    REQUIRE(v.create_gallery("a/c") == VaultResult::Ok);
    REQUIRE(v.create_gallery("d") == VaultResult::Ok);

    REQUIRE(v.toggle_favorite("a/b") == VaultResult::Ok);
    REQUIRE(v.toggle_favorite("d") == VaultResult::Ok);

    auto favs = v.list_favorite_galleries();
    REQUIRE(favs.size() == 2);
    CHECK_TRUE(has_path(favs, "a/b"));
    CHECK_TRUE(has_path(favs, "d"));
    CHECK_FALSE(has_path(favs, "a/c"));
    for (const auto& h : favs) {
        CHECK_TRUE(h.is_gallery);
        REQUIRE(h.node != nullptr);
        CHECK_TRUE(h.node->is_gallery());
    }

    // The two lists are disjoint by kind: no galleries in the image list.
    CHECK_TRUE(v.list_favorite_images().empty());
}

TEST(favorites_unfavorite_removes_from_list)
{
    TempVault tv("unfav");
    auto img = pattern(1000, 9);

    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kFavKdf, v) == VaultResult::Ok);
    REQUIRE(v.add_image("", img, "a.jpg") == VaultResult::Ok);
    REQUIRE(v.add_image("", img, "b.jpg") == VaultResult::Ok);

    REQUIRE(v.toggle_favorite("a.jpg") == VaultResult::Ok);
    REQUIRE(v.toggle_favorite("b.jpg") == VaultResult::Ok);
    REQUIRE(v.list_favorite_images().size() == 2);

    REQUIRE(v.toggle_favorite("a.jpg") == VaultResult::Ok);  // un-favorite
    auto favs = v.list_favorite_images();
    REQUIRE(favs.size() == 1);
    CHECK_TRUE(has_path(favs, "b.jpg"));
    CHECK_FALSE(has_path(favs, "a.jpg"));
}

TEST(favorites_toggle_missing_path_returns_not_found)
{
    TempVault tv("missing");

    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kFavKdf, v) == VaultResult::Ok);
    CHECK_EQ(v.toggle_favorite("nope.jpg"), VaultResult::NotFound);
}

TEST(favorites_operations_on_locked_vault_fail)
{
    TempVault tv("locked");

    {
        Vault v;
        REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kFavKdf, v) == VaultResult::Ok);
        v.lock();
    }

    Vault v2;
    REQUIRE(Vault::open(tv.str(), v2) == VaultResult::Ok);  // opens LOCKED
    CHECK_EQ(v2.toggle_favorite("anything"), VaultResult::Locked);
    CHECK_TRUE(v2.list_favorite_images().empty());
    CHECK_TRUE(v2.list_favorite_galleries().empty());
}
