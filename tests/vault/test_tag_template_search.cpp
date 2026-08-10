#include "test_framework.h"

#include <algorithm>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "ui/advanced_search_model.h"
#include "vault/file_util.h"
#include "vault/vault.h"
#include "vault/vault_search.h"

namespace fs = std::filesystem;

using vault::SearchScope;
using vault::Vault;
using vault::VaultResult;
using vault::VaultSearch;

// --- helpers --------------------------------------------------------------

// Cheap Argon2 params so tests stay fast.
static const crypto::KdfParams kKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

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
               ("osv_tagtplsearch_test_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
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

// --- tests ----------------------------------------------------------------

TEST(field_value_matches_carriers_in_plain_search)
{
    TempVault tv("plain");
    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == VaultResult::Ok);

    // One vault: image a.jpg tagged artist:bob (country=Japan), image b.jpg
    // untagged, gallery g tagged artist:ann (country=France) with child c.jpg.
    auto img = pattern(3000, 3);
    REQUIRE(v.add_image("", img, "a.jpg") == VaultResult::Ok);
    REQUIRE(v.add_image("", img, "b.jpg") == VaultResult::Ok);
    REQUIRE(v.create_gallery("g") == VaultResult::Ok);
    REQUIRE(v.add_image("g", img, "c.jpg") == VaultResult::Ok);
    REQUIRE(v.add_tag("a.jpg", "artist:bob") == VaultResult::Ok);
    REQUIRE(v.add_tag("g", "artist:ann") == VaultResult::Ok);

    auto s = vault::vault_settings(v);
    vault::set_tag_field_value(s, "artist:bob", "country", "Japan");
    vault::set_tag_field_value(s, "artist:ann", "country", "France");
    REQUIRE(vault::set_vault_settings(v, std::move(s)) == VaultResult::Ok);

    // Bare substring hits the value.
    auto hits = v.search("japan", SearchScope::Both);
    REQUIRE(hits.size() == 1u);
    CHECK_EQ(hits[0].path, std::string("a.jpg"));

    // Qualified form hits too.
    hits = v.search("country:Japan", SearchScope::Both);
    REQUIRE(hits.size() == 1u);
    CHECK_EQ(hits[0].path, std::string("a.jpg"));

    // Inherited carrier: c.jpg inherits artist:ann from g → France matches
    // both the gallery and the child image.
    hits = v.search("france", SearchScope::Both);
    CHECK_EQ(hits.size(), 2u);

    // Virtual tags never leak into effective_tags (display surface).
    hits = v.search("japan", SearchScope::Both);
    REQUIRE(hits.size() == 1u);
    for (const auto& t : hits[0].effective_tags)
        CHECK(t.find("country") == std::string::npos);
}

TEST(field_value_matches_in_advanced_search_and_exclude)
{
    TempVault tv("adv");
    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == VaultResult::Ok);

    // One vault: image a.jpg tagged artist:bob (country=Japan), image b.jpg
    // untagged, gallery g tagged artist:ann (country=France) with child c.jpg.
    auto img = pattern(3000, 3);
    REQUIRE(v.add_image("", img, "a.jpg") == VaultResult::Ok);
    REQUIRE(v.add_image("", img, "b.jpg") == VaultResult::Ok);
    REQUIRE(v.create_gallery("g") == VaultResult::Ok);
    REQUIRE(v.add_image("g", img, "c.jpg") == VaultResult::Ok);
    REQUIRE(v.add_tag("a.jpg", "artist:bob") == VaultResult::Ok);
    REQUIRE(v.add_tag("g", "artist:ann") == VaultResult::Ok);

    auto s = vault::vault_settings(v);
    vault::set_tag_field_value(s, "artist:bob", "country", "Japan");
    vault::set_tag_field_value(s, "artist:ann", "country", "France");
    REQUIRE(vault::set_vault_settings(v, std::move(s)) == VaultResult::Ok);

    ui::AdvancedQuery q;
    q.include = {{.tag = "country:Japan", .weight = 1}};
    q.scope   = ui::SearchScope::Images;
    auto hits = VaultSearch(v).run_search(q);
    REQUIRE(hits.size() == 1u);
    CHECK_EQ(hits[0].path, std::string("a.jpg"));

    // Exclude on a virtual tag rejects the carrier.
    ui::AdvancedQuery q2;
    q2.include = {{.tag = "artist:bob", .weight = 1}, {.tag = "artist:ann", .weight = 1}};
    q2.exclude = {"country:Japan"};
    q2.scope   = ui::SearchScope::Images;
    hits = VaultSearch(v).run_search(q2);
    // artist:bob carrier a.jpg has country:Japan virtual tag so gets excluded.
    // artist:ann carrier c.jpg (inherits it) has country:France virtual tag so stays.
    REQUIRE(hits.size() == 1u);
    CHECK_EQ(hits[0].path, std::string("g/c.jpg"));
}

TEST(field_values_absent_from_vocab_and_tallies)
{
    TempVault tv("vocab");
    Vault v;
    REQUIRE(Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == VaultResult::Ok);

    // One vault: image a.jpg tagged artist:bob (country=Japan), image b.jpg
    // untagged, gallery g tagged artist:ann (country=France) with child c.jpg.
    auto img = pattern(3000, 3);
    REQUIRE(v.add_image("", img, "a.jpg") == VaultResult::Ok);
    REQUIRE(v.add_image("", img, "b.jpg") == VaultResult::Ok);
    REQUIRE(v.create_gallery("g") == VaultResult::Ok);
    REQUIRE(v.add_image("g", img, "c.jpg") == VaultResult::Ok);
    REQUIRE(v.add_tag("a.jpg", "artist:bob") == VaultResult::Ok);
    REQUIRE(v.add_tag("g", "artist:ann") == VaultResult::Ok);

    auto s = vault::vault_settings(v);
    vault::set_tag_field_value(s, "artist:bob", "country", "Japan");
    vault::set_tag_field_value(s, "artist:ann", "country", "France");
    REQUIRE(vault::set_vault_settings(v, std::move(s)) == VaultResult::Ok);

    for (const auto& t : VaultSearch(v).all_tags())
        CHECK(t.find("country") == std::string::npos);
    for (const auto& tally : VaultSearch(v).tag_overview())
        CHECK(tally.tag.find("country") == std::string::npos);
}
