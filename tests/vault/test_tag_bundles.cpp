#include <algorithm>
#include "test_framework.h"
#include "vault/fixtures.h"
#include "vault/tag_bundles.h"
#include "vault/vault.h"

TEST(tag_bundles_manual_overlap_edit_delete_and_reopen)
{
    const auto path = std::filesystem::temp_directory_path() / "osv-bundles.osv";
    std::filesystem::remove_all(path);
    using enum vault::VaultResult;
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, path));
        REQUIRE(v.create_gallery("one") == Ok);
        REQUIRE(v.add_tag("one", "manual") == Ok);
        REQUIRE(vault::save_tag_bundle(v, "first", {"manual", "shared", "only"}) == Ok);
        REQUIRE(vault::save_tag_bundle(v, "second", {"shared"}) == Ok);
        const std::vector<std::string> paths{"one"};
        REQUIRE(vault::apply_tag_bundle(v, paths, "first", false) == Ok);
        REQUIRE(vault::apply_tag_bundle(v, paths, "second", false) == Ok);
        REQUIRE(v.add_tag("one", "only") == Ok);
        REQUIRE(vault::apply_tag_bundle(v, paths, "first", true) == Ok);
        CHECK_EQ(v.resolve_node("one")->tags.size(), size_t{3});
        REQUIRE(v.remove_tag("one", "shared") == Ok);
        CHECK_EQ(v.resolve_node("one")->tags.size(), size_t{3});
        REQUIRE(vault::save_tag_bundle(v, "second", {"replacement"}) == Ok);
        REQUIRE(vault::rename_tag(v, "replacement", "renamed") == Ok);
        REQUIRE(vault::delete_tag_bundle(v, "second") == Ok);
        CHECK_EQ(v.resolve_node("one")->tags.size(), size_t{2});
        REQUIRE(vault::save_tag_bundle(v, "renamed bundle", {"manual", "shared", "only"},
                                       "first") == Ok);
        REQUIRE(vault::apply_tag_bundle(v, paths, "renamed bundle", false) == Ok);
    }
    {
        vault::Vault v;
        REQUIRE(vaulttest::open_vault(path, v));
        const std::vector<std::string> paths{"one"};
        REQUIRE(vault::apply_tag_bundle(v, paths, "renamed bundle", true) == Ok);
        CHECK_EQ(v.resolve_node("one")->tags.size(), size_t{2});
        CHECK(v.resolve_node("one")->tags[0] == "manual");
        CHECK(v.resolve_node("one")->tags[1] == "only");
    }
    std::filesystem::remove_all(path);
}

TEST(tag_bundles_cancel_and_inheritance_independent)
{
    const auto path = std::filesystem::temp_directory_path() / "osv-bundles-cancel.osv";
    std::filesystem::remove_all(path);
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, path));
        REQUIRE(v.create_gallery("one") == vault::VaultResult::Ok);
        REQUIRE(v.add_tag("", "inherited") == vault::VaultResult::Ok);
        REQUIRE(vault::save_tag_bundle(v, "b", {"inherited"}) == vault::VaultResult::Ok);
        vault::OpProgress progress;
        progress.cancel.store(true);
        const std::vector<std::string> paths{"one", "one"};
        REQUIRE(vault::apply_tag_bundle(v, paths, "b", false, &progress) == vault::VaultResult::Ok);
        CHECK(v.resolve_node("one")->tags.empty());
        progress.cancel.store(false);
        REQUIRE(vault::apply_tag_bundle(v, paths, "b", false, &progress) == vault::VaultResult::Ok);
        REQUIRE(vault::delete_tag_bundle(v, "b") == vault::VaultResult::Ok);
        CHECK(v.resolve_node("one")->tags.empty());
        CHECK_EQ(v.resolve_node("")->tags.size(), size_t{1});
    }
    std::filesystem::remove_all(path);
}

TEST(tag_bundle_transfer_conflict_keeps_provenance_and_destination_definition)
{
    const auto dir = std::filesystem::temp_directory_path() / "osv-bundle-transfer";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    using enum vault::VaultResult;
    {
        vault::Vault src, dst;
        REQUIRE(vaulttest::make_directory_vault(src, dir / "source.osv"));
        REQUIRE(vaulttest::make_directory_vault(dst, dir / "destination.osv"));
        REQUIRE(src.create_gallery("source") == Ok);
        REQUIRE(dst.create_gallery("destination") == Ok);
        REQUIRE(vault::save_tag_bundle(src, "same name", {"source tag"}) == Ok);
        REQUIRE(vault::save_tag_bundle(dst, "same name", {"destination tag"}) == Ok);
        const std::vector<std::string> paths{"source"};
        REQUIRE(vault::apply_tag_bundle(src, paths, "same name", false) == Ok);
        auto* target = dst.resolve_node("destination");
        target->tags = src.resolve_node("source")->tags;
        REQUIRE(vault::copy_tag_provenance(*src.resolve_node("source"), vault::vault_settings(src),
                                           *target, dst) == Ok);
        CHECK(target->explicit_tags.empty());
        REQUIRE(vault::vault_settings(dst).tag_bundles.size() == 2);
        CHECK(vault::vault_settings(dst).tag_bundles[0].tags[0] == "destination tag");
        CHECK(vault::vault_settings(dst).tag_bundles[1].name == "same name (import 2)");
        REQUIRE(vault::set_vault_settings(dst, vault::vault_settings(dst)) == Ok);
    }
    {
        vault::Vault dst;
        REQUIRE(vaulttest::open_vault(dir / "destination.osv", dst));
        const std::vector<std::string> paths{"destination"};
        REQUIRE(vault::apply_tag_bundle(dst, paths, "same name (import 2)", true) == Ok);
        CHECK(dst.resolve_node("destination")->tags.empty());
    }
    std::filesystem::remove_all(dir);
}
