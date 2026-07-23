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

static std::vector<uint8_t> blob(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 17 + seed);
    return v;
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
               ("osv_rename_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
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

TEST(rename_image_updates_name_and_persists)
{
    using enum vault::VaultResult;
    TempVault tv("img");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.add_image("", blob(500, 1), "old.jpg") == Ok);
    REQUIRE(v.set_tags("old.jpg", {"vacation"}) == Ok);

    REQUIRE(vault::rename_node(v, "", "old.jpg", "new.jpg") == Ok);
    CHECK(find_child(v, "", "old.jpg") == nullptr);
    const auto* n = find_child(v, "", "new.jpg");
    REQUIRE(n != nullptr);
    CHECK(n->is_image());
    REQUIRE(n->tags.size() == 1);
    CHECK(n->tags[0] == "vacation");   // tags travel with the node, untouched by rename

    // Persists across reopen.
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == Ok);
    REQUIRE(v2.unlock(bytes("p"), {}) == Ok);
    CHECK(find_child(v2, "", "old.jpg") == nullptr);
    CHECK(find_child(v2, "", "new.jpg") != nullptr);
}

TEST(rename_gallery_updates_name_children_unaffected)
{
    using enum vault::VaultResult;
    TempVault tv("gal");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("Trips") == Ok);
    REQUIRE(v.add_image("Trips", blob(500, 2), "a.jpg") == Ok);
    REQUIRE(v.toggle_favorite("Trips") == Ok);

    REQUIRE(vault::rename_node(v, "", "Trips", "Journeys") == Ok);
    CHECK(find_child(v, "", "Trips") == nullptr);
    const auto* g = find_child(v, "", "Journeys");
    REQUIRE(g != nullptr);
    CHECK(g->is_gallery());
    CHECK(g->favorite);                                   // favorite flag travels with the node
    CHECK(find_child(v, "Journeys", "a.jpg") != nullptr);  // child list untouched
}

TEST(rename_to_same_name_is_a_no_op_ok)
{
    using enum vault::VaultResult;
    TempVault tv("noop");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.add_image("", blob(200, 3), "x.jpg") == Ok);
    CHECK(vault::rename_node(v, "", "x.jpg", "x.jpg") == Ok);
    CHECK(find_child(v, "", "x.jpg") != nullptr);
}

TEST(rename_collision_with_sibling_rejected)
{
    using enum vault::VaultResult;
    TempVault tv("collide");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.add_image("", blob(200, 4), "a.jpg") == Ok);
    REQUIRE(v.add_image("", blob(200, 5), "b.jpg") == Ok);
    CHECK(vault::rename_node(v, "", "a.jpg", "b.jpg") == AlreadyExists);
    CHECK(find_child(v, "", "a.jpg") != nullptr);   // untouched on rejection
}

TEST(rename_missing_source_returns_not_found)
{
    using enum vault::VaultResult;
    TempVault tv("missing");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    CHECK(vault::rename_node(v, "", "nope.jpg", "still-nope.jpg") == NotFound);
}

TEST(rename_unsafe_new_name_rejected)
{
    using enum vault::VaultResult;
    TempVault tv("unsafe");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.add_image("", blob(200, 6), "a.jpg") == Ok);
    CHECK(vault::rename_node(v, "", "a.jpg", "../escape.jpg") == InvalidArg);
    CHECK(find_child(v, "", "a.jpg") != nullptr);
}

TEST(rename_locked_vault_rejected)
{
    using enum vault::VaultResult;
    TempVault tv("locked");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.add_image("", blob(200, 7), "a.jpg") == Ok);

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == Ok);   // open but not unlocked
    CHECK(vault::rename_node(v2, "", "a.jpg", "b.jpg") == Locked);
}
