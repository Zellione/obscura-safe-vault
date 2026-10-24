#include <algorithm>
#include "test_framework.h"
#include "vault/tag_bundles.h"

TEST(bundle_model_preserves_manual_overlap_and_stable_rename_references)
{
    vault::IndexNode node;
    node.tags.emplace_back("manual");
    vault::initialize_tag_provenance(node);
    vault::VaultSettings settings;
    vault::TagBundle one, two;
    one.id[0] = 1;
    two.id[0] = 2;
    one.name = "one";
    two.name = "two";
    one.tags = {crypto::SecureString("manual"), crypto::SecureString("shared")};
    two.tags = {crypto::SecureString("shared")};
    settings.tag_bundles = {one, two};
    node.tag_bundles = {one.id, two.id};
    REQUIRE(vault::rebuild_bundle_tags(node, settings));
    CHECK_EQ(node.tags.size(), size_t{2});
    node.explicit_tags.emplace_back("shared");
    node.tag_bundles.clear();
    REQUIRE(vault::rebuild_bundle_tags(node, settings));
    CHECK_EQ(node.tags.size(), size_t{2});
    vault::visit_bundle_tag_references(node, settings, [](crypto::SecureString& tag) {
        if (tag == "shared") tag = "renamed";
    });
    REQUIRE(vault::rebuild_bundle_tags(node, settings));
    CHECK(node.tags[1] == "renamed");
    CHECK(settings.tag_bundles[1].tags[0] == "renamed");
    CHECK(settings.tag_bundles[1].id == two.id);
}

TEST(bundle_model_missing_reference_and_tag_limit_leave_effective_tags_unchanged)
{
    vault::IndexNode node;
    node.tags.emplace_back("original");
    vault::initialize_tag_provenance(node);
    std::array<uint8_t, crypto::NODE_ID_SIZE> id{};
    id[0] = 1;
    node.tag_bundles.push_back(id);
    vault::VaultSettings settings;
    CHECK_FALSE(vault::rebuild_bundle_tags(node, settings));
    CHECK(node.tags[0] == "original");
    vault::TagBundle bundle;
    bundle.id = id;
    bundle.name = "large";
    for (unsigned i = 0; i < vault::INDEX_MAX_TAGS; ++i)
        bundle.tags.emplace_back(std::to_string(i));
    settings.tag_bundles.push_back(std::move(bundle));
    CHECK_FALSE(vault::rebuild_bundle_tags(node, settings));
    CHECK_EQ(node.tags.size(), size_t{1});
}
