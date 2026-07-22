#include "test_framework.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vault/vault.h"

namespace fs = std::filesystem;

static const crypto::KdfParams kKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

// Internal linkage: several vault test files each define their own `TempVault`
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
               ("osv_rmg_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec; fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

}  // namespace

static std::vector<uint8_t> blob(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 17 + seed);
    return v;
}

static const vault::IndexNode* find_child(const vault::Vault& v, std::string_view gallery,
                                          std::string_view name)
{
    for (const auto* c : v.list(gallery)) if (c->name == name) return c;
    return nullptr;
}

TEST(remove_gallery_drops_subtree)
{
    using enum vault::VaultResult;
    TempVault tv("drop");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("Trips/2024") == Ok);
    REQUIRE(v.add_image("Trips/2024", blob(2000, 1), "a.jpg") == Ok);
    REQUIRE(v.add_image("Trips/2024", blob(2000, 2), "b.jpg") == Ok);

    REQUIRE(v.remove_gallery("Trips") == Ok);
    CHECK(find_child(v, "", "Trips") == nullptr);    // whole subtree gone
}

TEST(remove_gallery_persists_across_reopen)
{
    using enum vault::VaultResult;
    TempVault tv("persist");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
        REQUIRE(v.create_gallery("A/B") == Ok);
        REQUIRE(v.add_image("A/B", blob(1000, 3), "x.jpg") == Ok);
        REQUIRE(v.remove_gallery("A") == Ok);
    }
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == Ok);
    REQUIRE(v2.unlock(bytes("p"), {}) == Ok);
    CHECK(find_child(v2, "", "A") == nullptr);
}

TEST(remove_gallery_orphans_chunks_reclaimable_by_compact)
{
    using enum vault::VaultResult;
    TempVault tv("waste");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("Big") == Ok);
    // A chunk large enough to exceed the auto-compact floor on its own would be
    // reclaimed immediately; use a small one so we can observe wasted_bytes > 0
    // before an explicit compact.
    REQUIRE(v.add_image("Big", blob(4096, 5), "p.jpg") == Ok);
    REQUIRE(v.remove_gallery("Big") == Ok);

    CHECK(v.wasted_bytes() > 0);          // the orphaned chunk is now waste
    REQUIRE(v.compact() == Ok);
    CHECK_EQ(v.wasted_bytes(), static_cast<uint64_t>(0));   // reclaimed
}

TEST(remove_gallery_nested_subtree)
{
    using enum vault::VaultResult;
    TempVault tv("nested");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("Root/Mid/Leaf") == Ok);
    REQUIRE(v.add_image("Root/Mid/Leaf", blob(1500, 7), "img.jpg") == Ok);
    REQUIRE(v.create_gallery("Root/Other") == Ok);

    REQUIRE(v.remove_gallery("Root/Mid") == Ok);     // remove an inner subtree
    CHECK(find_child(v, "Root", "Mid") == nullptr);
    CHECK(find_child(v, "Root", "Other") != nullptr); // sibling untouched
}

TEST(remove_gallery_missing_returns_not_found)
{
    using enum vault::VaultResult;
    TempVault tv("missing");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    CHECK(v.remove_gallery("Nope") == NotFound);
}

TEST(remove_gallery_on_image_path_returns_not_found)
{
    using enum vault::VaultResult;
    TempVault tv("img");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.add_image("", blob(1000, 9), "pic.jpg") == Ok);
    CHECK(v.remove_gallery("pic.jpg") == NotFound);   // names an image, not a gallery
}

TEST(remove_gallery_root_returns_invalid_arg)
{
    using enum vault::VaultResult;
    TempVault tv("root");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    CHECK(v.remove_gallery("") == InvalidArg);
}

TEST(remove_gallery_locked_returns_locked)
{
    using enum vault::VaultResult;
    TempVault tv("locked");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
        REQUIRE(v.create_gallery("G") == Ok);
    }
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == Ok);   // opened but not unlocked
    CHECK(v2.remove_gallery("G") == Locked);
}
