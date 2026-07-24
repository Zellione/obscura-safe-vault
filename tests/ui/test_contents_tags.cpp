#include "test_framework.h"

#include <algorithm>
#include <string>
#include <vector>

#include "ui/tag_inherit.h"
#include "ui/zip_test_helpers.h"
#include "vault/vault.h"

// Tests for ui::contents_tags — the descendant-gallery tag union shown as the
// read-only "From contents" section (Phase 51 mirror of Phase 12's downward
// cascade). Must de-duplicate case-insensitively, exclude own and inherited tags,
// and respect depth bounds.

namespace fs = std::filesystem;
using ziptest::cleanup_dir;
using ziptest::fake_jpeg;
using ziptest::fresh_dir;
using ziptest::make_vault;

namespace {

bool has(const std::vector<std::string>& v, std::string_view s)
{
    return std::ranges::any_of(v, [s](const std::string& e) { return ui::tag_ci_equal(e, s); });
}

} // namespace

TEST(contents_tags_unions_descendant_tags)
{
    auto dir = fresh_dir("osv_contents_union");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(v.create_gallery("trip") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("trip", "vacation") == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("trip/day1") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("trip/day1", "beach") == vault::VaultResult::Ok);
        const auto img = fake_jpeg(1);
        REQUIRE(v.add_image("trip/day1", img, "a.jpg") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("trip/day1/a.jpg", "sunset") == vault::VaultResult::Ok);

        // trip has descendants tagged beach (gallery) and sunset (image).
        const auto t = ui::contents_tags(v, "trip");
        CHECK(has(t, "beach"));
        CHECK(has(t, "sunset"));
    }
    cleanup_dir(dir);
}

TEST(contents_tags_excludes_the_gallerys_own_tags)
{
    auto dir = fresh_dir("osv_contents_own");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(v.create_gallery("trip") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("trip", "vacation") == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("trip/day1") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("trip/day1", "beach") == vault::VaultResult::Ok);
        const auto img = fake_jpeg(1);
        REQUIRE(v.add_image("trip/day1", img, "a.jpg") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("trip/day1/a.jpg", "sunset") == vault::VaultResult::Ok);

        const auto t = ui::contents_tags(v, "trip");
        CHECK(!has(t, "vacation"));   // own tag, shown in the "own" section instead
    }
    cleanup_dir(dir);
}

TEST(contents_tags_excludes_inherited_tags)
{
    auto dir = fresh_dir("osv_contents_inherit");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(v.create_gallery("trip") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("trip", "vacation") == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("trip/day1") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("trip/day1", "beach") == vault::VaultResult::Ok);

        // trip/day1 inherits "vacation" from its ancestor, which must not appear
        // as "from contents" — it already has its own read-only section.
        const auto t = ui::contents_tags(v, "trip/day1");
        CHECK(!has(t, "vacation"));
    }
    cleanup_dir(dir);
}

TEST(contents_tags_dedupes_case_insensitively)
{
    auto dir = fresh_dir("osv_contents_dupe");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(v.create_gallery("trip") == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("trip/day1") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("trip/day1", "Beach") == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("trip/day2") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("trip/day2", "beach") == vault::VaultResult::Ok);  // ci duplicate

        // Two descendants tagged "Beach" and "beach" yield exactly one entry.
        const auto t = ui::contents_tags(v, "trip");
        CHECK_EQ(std::ranges::count_if(t,
            [](const std::string& s) { return ui::tag_ci_equal(s, "beach"); }), 1);
    }
    cleanup_dir(dir);
}

TEST(contents_tags_of_an_empty_gallery_is_empty)
{
    auto dir = fresh_dir("osv_contents_empty");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(v.create_gallery("empty_gallery") == vault::VaultResult::Ok);
        CHECK(ui::contents_tags(v, "empty_gallery").empty());
    }
    cleanup_dir(dir);
}

TEST(contents_tags_of_a_missing_path_is_empty)
{
    auto dir = fresh_dir("osv_contents_missing");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        CHECK(ui::contents_tags(v, "no/such/gallery").empty());
    }
    cleanup_dir(dir);
}
