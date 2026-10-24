#include "test_framework.h"
#include "vault/file_util.h"
#include "vault/fixtures.h"
#include "vault/vault.h"

TEST(tag_selection_commit_failure_restores_live_assignments)
{
    const auto path = std::filesystem::temp_directory_path() / "osv-tag-selection-failure.osv";
    std::filesystem::remove_all(path);
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_legacy_writable_vault(v, path));
        REQUIRE(v.create_gallery("one") == vault::VaultResult::Ok);
        const std::vector<std::string> paths{"one"};
        vault::fileutil::inject_sync_failure(0);
        const auto result = vault::add_tag_batch(v, paths, "new");
        vault::fileutil::clear_sync_failure();
        CHECK(result == vault::VaultResult::IoError);
        CHECK(v.resolve_node("one")->tags.empty());
        REQUIRE(vault::add_tag_batch(v, paths, "old") == vault::VaultResult::Ok);
        vault::fileutil::inject_sync_failure(0);
        const auto removed = vault::remove_tag_batch(v, paths, "old");
        vault::fileutil::clear_sync_failure();
        CHECK(removed == vault::VaultResult::IoError);
        REQUIRE(v.resolve_node("one")->tags.size() == 1);
        CHECK(v.resolve_node("one")->tags[0] == "old");
    }
    std::filesystem::remove_all(path);
}

TEST(tag_selection_cancel_preserves_assignments_and_duplicate_paths)
{
    const auto path = std::filesystem::temp_directory_path() / "osv-tag-selection-cancel.osv";
    std::filesystem::remove_all(path);
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, path));
        REQUIRE(v.create_gallery("one") == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("two") == vault::VaultResult::Ok);
        const std::vector<std::string> paths{"one", "two", "one", "missing"};
        vault::OpProgress progress;
        progress.cancel.store(true);
        CHECK(vault::mutate_tags_batch(v, paths, "new", false, &progress) ==
              vault::VaultResult::Ok);
        CHECK(v.resolve_node("one")->tags.empty());
        CHECK(v.resolve_node("two")->tags.empty());
        progress.cancel.store(false);
        REQUIRE(vault::mutate_tags_batch(v, paths, "new", false, &progress) ==
                vault::VaultResult::Ok);
        CHECK_EQ(v.resolve_node("one")->tags.size(), size_t{1});
        CHECK_EQ(v.resolve_node("two")->tags.size(), size_t{1});
        CHECK_EQ(progress.done.load(), progress.total.load());
        REQUIRE(vault::mutate_tags_batch(v, paths, "NEW", true) == vault::VaultResult::Ok);
        CHECK(v.resolve_node("one")->tags.empty());
    }
    std::filesystem::remove_all(path);
}

TEST(tag_selection_resolves_mixed_parents_root_and_missing_without_sorting)
{
    const auto path = std::filesystem::temp_directory_path() / "osv-tag-selection-resolve.osv";
    std::filesystem::remove_all(path);
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, path));
        REQUIRE(v.create_gallery("one") == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("one/two") == vault::VaultResult::Ok);
        const std::vector<std::string> paths{"one/two", "", "missing", "one", "one/two"};
        const auto nodes = vault::resolve_node_selection(v, paths);
        REQUIRE(nodes.size() == paths.size());
        CHECK(nodes[0] == v.resolve_node("one/two"));
        CHECK(nodes[1] == v.resolve_node(""));
        CHECK(nodes[2] == nullptr);
        CHECK(nodes[3] == v.resolve_node("one"));
        CHECK(nodes[4] == nodes[0]);
    }
    std::filesystem::remove_all(path);
}
