#include "test_framework.h"
#include "ui/tag_inherit.h"
#include "ui/zip_test_helpers.h"
#include "vault/vault.h"

#include <string>

// Tests for ui::inherited_tags — the ancestor-gallery tag union shown as the
// read-only "Inherited" section of the tag editor (Phase 27 follow-up). Must
// mirror the search cascade: every ancestor gallery's tags, in root→parent
// order, case-insensitively de-duplicated, minus the node's own tags.

namespace fs = std::filesystem;
using ziptest::cleanup_dir;
using ziptest::fake_jpeg;
using ziptest::fresh_dir;
using ziptest::make_vault;

TEST(inherited_tags_union_of_ancestors)
{
    auto dir = fresh_dir("osv_tag_inherit");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(v.create_gallery("A") == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("A/B") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("A", "outer") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("A/B", "inner") == vault::VaultResult::Ok);
        const auto img = fake_jpeg(1);
        REQUIRE(v.add_image("A/B", img, "1.jpg") == vault::VaultResult::Ok);

        // Image inherits from both ancestor galleries, root-first.
        const auto tags = ui::inherited_tags(v, "A/B/1.jpg");
        REQUIRE(tags.size() == static_cast<size_t>(2));
        CHECK_EQ(tags[0], std::string("outer"));
        CHECK_EQ(tags[1], std::string("inner"));

        // A sub-gallery inherits from its ancestors, not from itself.
        const auto sub = ui::inherited_tags(v, "A/B");
        REQUIRE(sub.size() == static_cast<size_t>(1));
        CHECK_EQ(sub[0], std::string("outer"));

        // A root node has no ancestors to inherit from.
        CHECK(ui::inherited_tags(v, "A").empty());
    }
    cleanup_dir(dir);
}

TEST(inherited_tags_dedupes_and_hides_own)
{
    auto dir = fresh_dir("osv_tag_inherit_dupe");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(v.create_gallery("A") == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("A/B") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("A", "Shared") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("A", "only-outer") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("A/B", "shared") == vault::VaultResult::Ok);  // ci duplicate
        const auto img = fake_jpeg(2);
        REQUIRE(v.add_image("A/B", img, "1.jpg") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("A/B/1.jpg", "ONLY-OUTER") == vault::VaultResult::Ok);

        // "Shared" appears once (ci de-dupe across ancestors); "only-outer" is
        // hidden because the node carries it itself (own tags win).
        const auto tags = ui::inherited_tags(v, "A/B/1.jpg");
        REQUIRE(tags.size() == static_cast<size_t>(1));
        CHECK_EQ(tags[0], std::string("Shared"));
    }
    cleanup_dir(dir);
}

TEST(inherited_tags_invalid_path_is_empty)
{
    auto dir = fresh_dir("osv_tag_inherit_bad");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        CHECK(ui::inherited_tags(v, "").empty());
        CHECK(ui::inherited_tags(v, "no/such/node.jpg").empty());
    }
    cleanup_dir(dir);
}
