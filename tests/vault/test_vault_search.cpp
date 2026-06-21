// Phase 18: vault-level advanced search — distinct tag vocabulary (all_tags),
// saved searches persisted in the encrypted index across lock/reopen, and
// run_search evaluating a weighted include/exclude query with scope + ranking.

#include "test_framework.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "ui/advanced_search_model.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

namespace {

const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_search_test_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

bool has_path(const std::vector<vault::SearchHit>& hits, std::string_view p)
{
    return std::ranges::any_of(hits, [&](const auto& h) { return h.path == p; });
}

} // namespace

TEST(search_all_tags_dedups_case_insensitively_across_tree)
{
    TempVault tv("alltags");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("trip") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("trip", pattern(2000, 1), "a.jpg") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("trip", pattern(2000, 2), "b.jpg") == vault::VaultResult::Ok);

    REQUIRE(v.set_tags("trip", {"Vacation"}) == vault::VaultResult::Ok);
    REQUIRE(v.set_tags("trip/a.jpg", {"beach", "SUNSET"}) == vault::VaultResult::Ok);
    REQUIRE(v.set_tags("trip/b.jpg", {"sunset", "Beach"}) == vault::VaultResult::Ok);

    auto vocab = v.all_tags();
    // Distinct case-insensitively: vacation, beach, sunset → 3 entries.
    CHECK_EQ(vocab.size(), static_cast<size_t>(3));
    auto present = [&](std::string_view t) {
        return std::ranges::any_of(vocab, [&](const auto& x) {
            return x.size() == t.size() &&
                   std::equal(x.begin(), x.end(), t.begin(), [](char a, char b) {
                       return std::tolower(a) == std::tolower(b);
                   });
        });
    };
    CHECK(present("vacation"));
    CHECK(present("beach"));
    CHECK(present("sunset"));
}

TEST(search_saved_searches_round_trip_across_reopen)
{
    TempVault tv("saved");

    ui::AdvancedQuery q;
    q.include = {ui::WeightedTag{"cat", 2}};
    q.exclude = {"blurry"};
    q.scope   = ui::SearchScope::Images;

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
        CHECK(v.list_saved_searches().empty());
        REQUIRE(v.save_search("my cats", q) == vault::VaultResult::Ok);
        REQUIRE(v.list_saved_searches().size() == 1);
    }

    // Reopen: the saved search survives lock + reopen.
    {
        vault::Vault v;
        REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
        REQUIRE(v.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

        auto saved = v.list_saved_searches();
        REQUIRE(saved.size() == 1);
        CHECK_EQ(saved[0].name, std::string("my cats"));

        ui::AdvancedQuery back;
        REQUIRE(ui::deserialize_query(saved[0].query, back));
        REQUIRE(back.include.size() == 1);
        CHECK_EQ(back.include[0].tag, std::string("cat"));
        CHECK_EQ(back.include[0].weight, 2);
        CHECK(back.scope == ui::SearchScope::Images);

        // Delete it; deletion persists too.
        REQUIRE(v.delete_saved_search("my cats") == vault::VaultResult::Ok);
        CHECK(v.list_saved_searches().empty());
        CHECK(v.delete_saved_search("nope") == vault::VaultResult::NotFound);
    }

    {
        vault::Vault v;
        REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
        REQUIRE(v.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);
        CHECK(v.list_saved_searches().empty());
    }
}

TEST(search_save_search_upserts_by_name)
{
    TempVault tv("upsert");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);

    ui::AdvancedQuery q1; q1.name_query = "first";
    ui::AdvancedQuery q2; q2.name_query = "second";
    REQUIRE(v.save_search("s", q1) == vault::VaultResult::Ok);
    REQUIRE(v.save_search("s", q2) == vault::VaultResult::Ok);  // replace, not duplicate

    auto saved = v.list_saved_searches();
    REQUIRE(saved.size() == 1);
    ui::AdvancedQuery back;
    REQUIRE(ui::deserialize_query(saved[0].query, back));
    CHECK_EQ(back.name_query, std::string("second"));

    CHECK(v.save_search("", q1) == vault::VaultResult::InvalidArg);  // empty name rejected
}

TEST(search_run_search_filters_ranks_and_scopes)
{
    TempVault tv("run");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("pics") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("pics", pattern(2000, 1), "cat1.jpg") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("pics", pattern(2000, 2), "dog1.jpg") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("pics", pattern(2000, 3), "both.jpg") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("pics", pattern(2000, 4), "bad.jpg")  == vault::VaultResult::Ok);

    REQUIRE(v.set_tags("pics/cat1.jpg", {"cat"}) == vault::VaultResult::Ok);
    REQUIRE(v.set_tags("pics/dog1.jpg", {"dog"}) == vault::VaultResult::Ok);
    REQUIRE(v.set_tags("pics/both.jpg", {"cat", "dog"}) == vault::VaultResult::Ok);
    REQUIRE(v.set_tags("pics/bad.jpg",  {"cat", "blurry"}) == vault::VaultResult::Ok);

    ui::AdvancedQuery q;
    q.include = {ui::WeightedTag{"cat", 3}, ui::WeightedTag{"dog", 1}};
    q.exclude = {"blurry"};
    q.scope   = ui::SearchScope::Images;

    auto hits = v.run_search(q);
    // cat1, dog1, both match; bad.jpg excluded.
    CHECK_EQ(hits.size(), static_cast<size_t>(3));
    CHECK(has_path(hits, "pics/cat1.jpg"));
    CHECK(has_path(hits, "pics/dog1.jpg"));
    CHECK(has_path(hits, "pics/both.jpg"));
    CHECK_FALSE(has_path(hits, "pics/bad.jpg"));

    // Ranked by score: both (cat+dog = 4) first, then cat1 (3), then dog1 (1).
    REQUIRE(hits.size() == 3);
    CHECK_EQ(hits[0].path, std::string("pics/both.jpg"));
    CHECK_EQ(hits[1].path, std::string("pics/cat1.jpg"));
    CHECK_EQ(hits[2].path, std::string("pics/dog1.jpg"));
}

TEST(search_run_search_galleries_scope_uses_cascade)
{
    TempVault tv("scope");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("trip/day1") == vault::VaultResult::Ok);
    REQUIRE(v.set_tags("trip", {"vacation"}) == vault::VaultResult::Ok);

    ui::AdvancedQuery q;
    q.include = {ui::WeightedTag{"vacation", 1}};
    q.scope   = ui::SearchScope::Galleries;

    auto hits = v.run_search(q);
    // "trip" (owns vacation) and "trip/day1" (inherits it) both match; no images.
    CHECK(has_path(hits, "trip"));
    CHECK(has_path(hits, "trip/day1"));
    CHECK(std::ranges::all_of(hits, [](const auto& h) { return h.is_gallery; }));
}

TEST(search_run_search_empty_when_locked)
{
    TempVault tv("locked");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    }
    vault::Vault v;
    REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
    ui::AdvancedQuery q;
    CHECK(v.run_search(q).empty());
    CHECK(v.all_tags().empty());
    CHECK(v.list_saved_searches().empty());
    CHECK(v.save_search("x", q) == vault::VaultResult::Locked);
}
