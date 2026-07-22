#include "test_framework.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vault/transfer.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

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
               ("osv_xgs_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec; fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

}  // namespace

static const vault::IndexNode* find_child(const vault::Vault& v, std::string_view gallery,
                                          std::string_view name)
{
    for (const auto* c : v.list(gallery)) if (c->name == name) return c;
    return nullptr;
}

TEST(transfer_galleries_moves_every_listed_subtree)
{
    using enum vault::VaultResult;
    TempVault sa("src"), da("dst");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("Alpha") == Ok);
    REQUIRE(src.create_gallery("Beta") == Ok);
    REQUIRE(src.add_image("Alpha", blob(500, 1), "a.jpg") == Ok);
    REQUIRE(src.add_image("Beta", blob(500, 2), "b.jpg") == Ok);

    const auto t = vault::transfer_galleries(src, {"Alpha", "Beta"}, dst, "",
                                             vault::TransferMode::Move);
    CHECK(t.done == 2);
    CHECK(t.failed == 0);
    CHECK(find_child(src, "", "Alpha") == nullptr);
    CHECK(find_child(src, "", "Beta") == nullptr);
    CHECK(find_child(dst, "Alpha", "a.jpg") != nullptr);
    CHECK(find_child(dst, "Beta", "b.jpg") != nullptr);
}

TEST(transfer_galleries_tallies_collisions_as_failed_and_leaves_them_intact)
{
    using enum vault::VaultResult;
    TempVault sa("src2"), da("dst2");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("Dupe") == Ok);
    REQUIRE(src.create_gallery("Unique") == Ok);
    REQUIRE(dst.create_gallery("Dupe") == Ok);   // pre-existing collision

    const auto t = vault::transfer_galleries(src, {"Dupe", "Unique"}, dst, "",
                                             vault::TransferMode::Move);
    CHECK(t.done == 1);
    CHECK(t.failed == 1);
    CHECK(find_child(src, "", "Dupe") != nullptr);      // failed one stays
    CHECK(find_child(src, "", "Unique") == nullptr);    // succeeded one moved
    CHECK(find_child(dst, "", "Unique") != nullptr);
}

TEST(transfer_galleries_copy_mode_keeps_every_source)
{
    using enum vault::VaultResult;
    TempVault sa("src3"), da("dst3");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("One") == Ok);
    REQUIRE(src.create_gallery("Two") == Ok);

    const auto t = vault::transfer_galleries(src, {"One", "Two"}, dst, "",
                                             vault::TransferMode::Copy);
    CHECK(t.done == 2);
    CHECK(find_child(src, "", "One") != nullptr);
    CHECK(find_child(src, "", "Two") != nullptr);
    CHECK(find_child(dst, "", "One") != nullptr);
    CHECK(find_child(dst, "", "Two") != nullptr);
}

TEST(transfer_galleries_empty_list_returns_zero_tally)
{
    using enum vault::VaultResult;
    TempVault sa("src4"), da("dst4");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    const auto t = vault::transfer_galleries(src, {}, dst, "", vault::TransferMode::Move);
    CHECK(t.done == 0);
    CHECK(t.failed == 0);
}
