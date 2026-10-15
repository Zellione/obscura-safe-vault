// Phase 22: vault-level tag overview — per distinct tag, how many galleries and
// images *directly* carry it (no Phase 12 cascade), plus the galleries-only
// lookup that backs the overview's "open this tag" action.

#include "test_framework.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "ui/tag_overview_model.h"
#include "ui/advanced_search_model.h"
#include "vault/file_util.h"
#include "vault/vault.h"
#include "vault/vault_search.h"

namespace fs = std::filesystem;

namespace {

const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

std::vector<uint8_t> read_file(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_tagov_test_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove_all(path, ec); }
    std::string str() const { return path.string(); }
};

const ui::TagTally* find_tag(const std::vector<ui::TagTally>& v, std::string_view tag)
{
    auto it = std::ranges::find_if(v, [&](const ui::TagTally& t) {
        return t.tag.size() == tag.size() &&
               std::equal(t.tag.begin(), t.tag.end(), tag.begin(), [](char a, char b) {
                   return std::tolower(static_cast<unsigned char>(a)) ==
                          std::tolower(static_cast<unsigned char>(b));
               });
    });
    return it == v.end() ? nullptr : &*it;
}

bool has_path(const std::vector<vault::SearchHit>& hits, std::string_view p)
{
    return std::ranges::any_of(hits, [&](const auto& h) { return h.path == p; });
}

// Build the shared nested fixture used by several tests.
//   trip       gallery, tag "vacation"
//     day1     gallery, tag "beach"      → a.jpg {beach,sunset}, b.jpg {sunset}
//     day2     gallery, (no tags)        → c.jpg {vacation}, clip.mp4 {vacation}
//   misc       gallery, tag "beach"      (empty leaf)
[[nodiscard]] bool build_tree(vault::Vault& v)
{
    using R = vault::VaultResult;
    return v.create_gallery("trip/day1") == R::Ok &&
           v.create_gallery("trip/day2") == R::Ok &&
           v.create_gallery("misc") == R::Ok &&
           v.add_image("trip/day1", pattern(2000, 1), "a.jpg") == R::Ok &&
           v.add_image("trip/day1", pattern(2000, 2), "b.jpg") == R::Ok &&
           v.add_image("trip/day2", pattern(2000, 3), "c.jpg") == R::Ok &&
           v.set_tags("trip", {"vacation"}) == R::Ok &&
           v.set_tags("trip/day1", {"beach"}) == R::Ok &&
           v.set_tags("misc", {"beach"}) == R::Ok &&
           v.set_tags("trip/day1/a.jpg", {"beach", "sunset"}) == R::Ok &&
           v.set_tags("trip/day1/b.jpg", {"sunset"}) == R::Ok &&
           v.set_tags("trip/day2/c.jpg", {"vacation"}) == R::Ok &&
           v.add_video("trip/day2",
                       std::span<const uint8_t>(read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4")),
                       "clip.mp4", /*chunk_size=*/4096) == R::Ok &&
           v.set_tags("trip/day2/clip.mp4", {"vacation"}) == R::Ok;
}

} // namespace

TEST(tag_overview_counts_direct_tags_only_no_cascade)
{
    TempVault tv("counts");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(build_tree(v));

    auto ov = vault::VaultSearch(v).tag_overview();
    // Three distinct tags: vacation, beach, sunset.
    CHECK_EQ(ov.size(), static_cast<size_t>(3));

    // vacation: only "trip" gallery directly + c.jpg image directly. The cascade
    // onto day1/day2/a/b must NOT be counted.
    const auto* vac = find_tag(ov, "vacation");
    REQUIRE(vac != nullptr);
    CHECK_EQ(vac->gallery_count, 1);
    CHECK_EQ(vac->image_count, 2);   // c.jpg + clip.mp4 (videos count as media)

    // beach: day1 + misc galleries directly; a.jpg image directly.
    const auto* beach = find_tag(ov, "beach");
    REQUIRE(beach != nullptr);
    CHECK_EQ(beach->gallery_count, 2);
    CHECK_EQ(beach->image_count, 1);

    // sunset: no galleries; a.jpg + b.jpg images.
    const auto* sun = find_tag(ov, "sunset");
    REQUIRE(sun != nullptr);
    CHECK_EQ(sun->gallery_count, 0);
    CHECK_EQ(sun->image_count, 2);
}

TEST(tag_overview_galleries_with_tag_is_direct_only)
{
    TempVault tv("gwt");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(build_tree(v));
    vault::VaultSearch s(v);

    // vacation directly tags only "trip" (day1/day2 inherit but don't carry it).
    auto vac = s.galleries_with_tag("vacation");
    CHECK_EQ(vac.size(), static_cast<size_t>(1));
    CHECK(has_path(vac, "trip"));
    CHECK(std::ranges::all_of(vac, [](const auto& h) { return h.is_gallery; }));

    // beach directly tags day1 and misc (case-insensitive match).
    auto beach = s.galleries_with_tag("BEACH");
    CHECK_EQ(beach.size(), static_cast<size_t>(2));
    CHECK(has_path(beach, "trip/day1"));
    CHECK(has_path(beach, "misc"));

    // sunset is only on images → no galleries.
    CHECK(s.galleries_with_tag("sunset").empty());
}

TEST(tag_overview_empty_for_untagged_vault)
{
    TempVault tv("empty");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("plain") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("plain", pattern(2000, 1), "x.jpg") == vault::VaultResult::Ok);

    CHECK(vault::VaultSearch(v).tag_overview().empty());
    CHECK(vault::VaultSearch(v).galleries_with_tag("anything").empty());
}

TEST(tag_overview_empty_when_locked)
{
    TempVault tv("locked");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
        REQUIRE(build_tree(v));
    }
    vault::Vault v;
    REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);   // stays locked
    CHECK(vault::VaultSearch(v).tag_overview().empty());
    CHECK(vault::VaultSearch(v).galleries_with_tag("beach").empty());
}

TEST(tag_overview_stable_across_lock_reopen)
{
    TempVault tv("reopen");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
        REQUIRE(build_tree(v));
    }
    vault::Vault v;
    REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);
    REQUIRE(v.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    auto ov = vault::VaultSearch(v).tag_overview();
    CHECK_EQ(ov.size(), static_cast<size_t>(3));
    const auto* beach = find_tag(ov, "beach");
    REQUIRE(beach != nullptr);
    CHECK_EQ(beach->gallery_count, 2);
    CHECK_EQ(beach->image_count, 1);

    auto vac = vault::VaultSearch(v).galleries_with_tag("vacation");
    CHECK_EQ(vac.size(), static_cast<size_t>(1));
    CHECK(has_path(vac, "trip"));
}

TEST(images_with_tag_direct_only_includes_video)
{
    TempVault tv("iwt");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(build_tree(v));
    vault::VaultSearch s(v);

    // vacation directly tags c.jpg (image) and clip.mp4 (video); a.jpg/b.jpg only
    // inherit via the parent gallery and must NOT appear.
    auto vac = s.images_with_tag("vacation");
    CHECK_EQ(vac.size(), static_cast<size_t>(2));
    CHECK(has_path(vac, "trip/day2/c.jpg"));
    CHECK(has_path(vac, "trip/day2/clip.mp4"));
    CHECK(std::ranges::none_of(vac, [](const auto& h) { return h.is_gallery; }));

    // Case-insensitive: a.jpg carries beach directly; day1/misc galleries don't leak in.
    auto beach = s.images_with_tag("BEACH");
    CHECK_EQ(beach.size(), static_cast<size_t>(1));
    CHECK(has_path(beach, "trip/day1/a.jpg"));

    // sunset is on a.jpg + b.jpg only.
    auto sun = s.images_with_tag("sunset");
    CHECK_EQ(sun.size(), static_cast<size_t>(2));
    CHECK(has_path(sun, "trip/day1/a.jpg"));
    CHECK(has_path(sun, "trip/day1/b.jpg"));
}

TEST(images_with_tag_matches_overview_image_count)
{
    TempVault tv("iwt_consistency");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(build_tree(v));
    vault::VaultSearch s(v);

    for (const auto& tt : s.tag_overview())
        CHECK_EQ(s.images_with_tag(tt.tag).size(), static_cast<size_t>(tt.image_count));
}

TEST(images_with_tag_empty_and_locked)
{
    TempVault tv("iwt_empty");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
        REQUIRE(build_tree(v));
        CHECK(vault::VaultSearch(v).images_with_tag("nope").empty());   // unknown tag
        CHECK(vault::VaultSearch(v).images_with_tag("").empty());       // empty tag
    }
    vault::Vault v;
    REQUIRE(vault::Vault::open(tv.str(), v) == vault::VaultResult::Ok);   // stays locked
    CHECK(vault::VaultSearch(v).images_with_tag("sunset").empty());
}

TEST(tag_overview_rows_carry_the_stored_description)
{
    // Build a vault with one tagged gallery, store a description, and confirm
    // the overview row carries it.
    TempVault tv("desc");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(build_tree(v));

    auto s = vault::vault_settings(v);
    vault::set_tag_description(s, "vacation", "Summer trips only");
    CHECK(vault::set_vault_settings(v, std::move(s)) == vault::VaultResult::Ok);

    const auto rows = vault::VaultSearch(v).tag_overview();
    const auto it = std::ranges::find_if(rows,
        [](const ui::TagTally& r) { return r.tag == "vacation"; });
    CHECK(it != rows.end());
    CHECK_EQ(it->description, std::string("Summer trips only"));
}

TEST(tag_rename_updates_all_assignments_and_metadata_after_reopen)
{
    TempVault tv("rename");
    vault::Vault v;
    using R = vault::VaultResult;
    REQUIRE(vault::Vault::create_directory(tv.str(), bytes("pw"), {}, kTestKdf, v) == R::Ok);
    REQUIRE(build_tree(v));
    REQUIRE(vault::rename_tag(v, "vacation", "travel:vacation") == R::Ok);
    REQUIRE(v.set_tags("", {"travel:VACATION"}) == R::Ok);
    auto settings = vault::vault_settings(v);
    settings.categories.push_back({crypto::SecureString("travel"), 0,
                                   {crypto::SecureString("year")}});
    vault::set_tag_description(settings, "travel:Vacation", "Summer trips");
    vault::set_tag_field_value(settings, "travel:vacation", "year", "2026");
    REQUIRE(vault::set_vault_settings(v, std::move(settings)) == R::Ok);
    REQUIRE(vault::rename_tag(v, "travel:VACATION", "  travel:holiday  ") == R::Ok);
    CHECK(vault::VaultSearch(v).images_with_tag("travel:vacation").empty());
    CHECK_EQ(vault::VaultSearch(v).images_with_tag("travel:holiday").size(), size_t{2});
    CHECK_EQ(vault::find_tag_description(vault::vault_settings(v), "travel:holiday"), "Summer trips");
    CHECK_EQ(vault::find_tag_field_value(vault::vault_settings(v), "travel:holiday", "year"), "2026");
    CHECK(vault::rename_tag(v, "travel:holiday", "holiday") == R::InvalidArg);
    CHECK(vault::rename_tag(v, "travel:holiday", "unknown:holiday") == R::InvalidArg);
    v.lock();
    REQUIRE(v.unlock(bytes("pw"), {}) == R::Ok);
    CHECK_EQ(vault::VaultSearch(v).images_with_tag("travel:holiday").size(), size_t{2});
    CHECK_EQ(vault::find_tag_description(vault::vault_settings(v), "travel:holiday"), "Summer trips");
    CHECK_EQ(vault::find_tag_field_value(vault::vault_settings(v), "travel:holiday", "year"), "2026");
    CHECK(vault::find_tag_description(vault::vault_settings(v), "travel:vacation").empty());
}

TEST(tag_rename_rejects_collisions_and_invalid_names_without_changes)
{
    TempVault tv("rename_invalid");
    vault::Vault v;
    using R = vault::VaultResult;
    CHECK(vault::rename_tag(v, "old", "new") == R::Locked);
    REQUIRE(vault::Vault::create_directory(tv.str(), bytes("pw"), {}, kTestKdf, v) == R::Ok);
    REQUIRE(build_tree(v));
    auto settings = vault::vault_settings(v);
    vault::set_tag_description(settings, "reserved", "Unused dictionary tag");
    settings.categories.push_back({crypto::SecureString("travel"), 0,
                                   {crypto::SecureString("year")}});
    vault::set_tag_field_value(settings, "travel:field-only", "year", "2026");
    REQUIRE(vault::set_vault_settings(v, std::move(settings)) == R::Ok);
    CHECK(vault::rename_tag(v, "vacation", "BEACH") == R::AlreadyExists);
    CHECK(vault::rename_tag(v, "vacation", "RESERVED") == R::AlreadyExists);
    CHECK(vault::rename_tag(v, "vacation", "travel:FIELD-ONLY") == R::AlreadyExists);
    CHECK(vault::rename_tag(v, "vacation", "  ") == R::InvalidArg);
    CHECK(vault::rename_tag(v, "vacation", std::string(65536, 'x')) == R::InvalidArg);
    CHECK(vault::rename_tag(v, "vacation", std::string("bad\0name", 8)) == R::InvalidArg);
    CHECK(vault::rename_tag(v, "missing", "new") == R::NotFound);
    CHECK_EQ(vault::VaultSearch(v).images_with_tag("vacation").size(), size_t{2});
    REQUIRE(vault::rename_tag(v, "vacation", "VACATION") == R::Ok);
    const auto rows = vault::VaultSearch(v).tag_overview();
    const auto* tag = find_tag(rows, "vacation");
    REQUIRE(tag != nullptr);
    CHECK_EQ(tag->tag, "VACATION");
    CHECK(vault::rename_tag(v, "VACATION", "VACATION") == R::Ok);
}

TEST(tag_rename_preserves_saved_search_meaning)
{
    TempVault tv("rename_search");
    vault::Vault v;
    using R = vault::VaultResult;
    REQUIRE(vault::Vault::create_directory(tv.str(), bytes("pw"), {}, kTestKdf, v) == R::Ok);
    REQUIRE(build_tree(v));
    ui::AdvancedQuery query;
    query.include.push_back({"Vacation", 3});
    query.exclude.push_back("VACATION");
    query.groups.push_back({"vacation", ui::Combinator::And, {"vacation", "beach"}});
    query.name_query = "vacation";
    REQUIRE(vault::VaultSearch(v).save_search("vacation", query) == R::Ok);
    REQUIRE(vault::rename_tag(v, "vacation", "holiday") == R::Ok);
    v.lock();
    REQUIRE(v.unlock(bytes("pw"), {}) == R::Ok);
    const auto saved = vault::VaultSearch(v).list_saved_searches();
    REQUIRE(saved.size() == 1);
    ui::AdvancedQuery actual;
    REQUIRE(ui::deserialize_query(saved[0].query.as_span(), actual));
    CHECK_EQ(actual.include[0].tag, "holiday");
    CHECK_EQ(actual.include[0].weight, 3);
    CHECK_EQ(actual.exclude[0], "holiday");
    CHECK_EQ(actual.groups[0].tags[0], "holiday");
    CHECK_EQ(actual.groups[0].tags[1], "beach");
    CHECK_EQ(actual.name_query, "vacation");
    CHECK_EQ(saved[0].name, "vacation");
}

TEST(tag_rename_rolls_back_on_commit_failure_and_rejects_readonly)
{
    TempVault tv("rename_failure");
    vault::Vault v;
    using R = vault::VaultResult;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == R::Ok);
    REQUIRE(build_tree(v));
    auto settings = vault::vault_settings(v);
    vault::set_tag_description(settings, "vacation", "Summer");
    REQUIRE(vault::set_vault_settings(v, std::move(settings)) == R::Ok);
    vault::fileutil::inject_sync_failure(0);
    const auto result = vault::rename_tag(v, "vacation", "holiday");
    vault::fileutil::clear_sync_failure();
    CHECK(result == R::IoError);
    CHECK_EQ(vault::VaultSearch(v).images_with_tag("vacation").size(), size_t{2});
    CHECK(vault::VaultSearch(v).images_with_tag("holiday").empty());
    CHECK_EQ(vault::find_tag_description(vault::vault_settings(v), "vacation"), "Summer");
    v.lock();
    vault::Vault reopened;
    REQUIRE(vault::Vault::open(tv.str(), reopened) == R::Ok);
    REQUIRE(reopened.unlock(bytes("pw"), {}) == R::Ok);
    CHECK(vault::rename_tag(reopened, "vacation", "holiday") == R::InvalidArg);
    CHECK_EQ(vault::VaultSearch(reopened).images_with_tag("vacation").size(), size_t{2});
}
