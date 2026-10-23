#include <algorithm>
#include "test_framework.h"
#include "vault/fixtures.h"
#include "vault/tag_bundles.h"
#include "vault/transfer.h"
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

TEST(tag_bundles_set_and_batch_manual_ownership_survives_reopen)
{
    const auto path = std::filesystem::temp_directory_path() / "osv-bundle-direct-edit.osv";
    std::filesystem::remove_all(path);
    using enum vault::VaultResult;
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, path));
        REQUIRE(v.create_gallery("one") == Ok);
        REQUIRE(v.create_gallery("two") == Ok);
        REQUIRE(vault::save_tag_bundle(v, "a", {"shared", "a-only"}) == Ok);
        REQUIRE(vault::save_tag_bundle(v, "b", {"shared", "b-only"}) == Ok);
        const std::vector<std::string> paths{"one", "two", "one"};
        REQUIRE(vault::apply_tag_bundle(v, paths, "a", false) == Ok);
        REQUIRE(vault::apply_tag_bundle(v, paths, "b", false) == Ok);
        REQUIRE(v.set_tags("one", {"shared", "manual"}) == Ok);
        REQUIRE(vault::add_tag_batch(v, paths, "a-only") == Ok);
        REQUIRE(vault::remove_tag_batch(v, paths, "shared") == Ok);
        CHECK_EQ(v.resolve_node("one")->tags.size(), size_t{4});
        REQUIRE(vault::apply_tag_bundle(v, paths, "a", true) == Ok);
        CHECK_EQ(v.resolve_node("one")->tags.size(), size_t{4});
        REQUIRE(vault::delete_tag_bundle(v, "b") == Ok);
        CHECK_EQ(v.resolve_node("one")->tags.size(), size_t{2});
        REQUIRE(vault::remove_tag_batch(v, paths, "a-only") == Ok);
        REQUIRE(vault::apply_tag_bundle(v, paths, "a", false) == Ok);
        REQUIRE(v.set_tags("one", {}) == Ok);
        REQUIRE(vault::delete_tag_bundle(v, "a") == Ok);
        CHECK(v.resolve_node("one")->tags.empty());
        CHECK(v.resolve_node("two")->tags.empty());
    }
    {
        vault::Vault v;
        REQUIRE(vaulttest::open_vault(path, v));
        CHECK(v.resolve_node("one")->tags.empty());
        CHECK(v.resolve_node("two")->tags.empty());
    }
    std::filesystem::remove_all(path);
}

TEST(tag_bundles_transfer_subtree_keeps_internal_inheritance_and_boundary_manual_tags)
{
    const auto dir = std::filesystem::temp_directory_path() / "osv-bundle-subtree";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    using enum vault::VaultResult;
    {
        vault::Vault src, dst;
        REQUIRE(vaulttest::make_directory_vault(src, dir / "source.osv"));
        REQUIRE(vaulttest::make_directory_vault(dst, dir / "destination.osv"));
        REQUIRE(src.create_gallery("parent/tree/child") == Ok);
        REQUIRE(src.add_tag("parent", "boundary") == Ok);
        REQUIRE(vault::save_tag_bundle(src, "bundle", {"inherited", "boundary"}) == Ok);
        REQUIRE(vault::apply_tag_bundle(src, std::vector<std::string>{"parent/tree"}, "bundle",
                                        false) == Ok);
        const std::array<uint8_t, 4> data{1, 2, 3, 4};
        REQUIRE(src.add_image("parent/tree/child", data, "file.bin") == Ok);
        REQUIRE(vault::transfer_gallery(src, "parent/tree", dst, "", vault::TransferMode::Copy) ==
                Ok);
        REQUIRE(dst.resolve_node("tree/child/file.bin"));
        CHECK(dst.resolve_node("tree/child/file.bin")->tags.empty());
        REQUIRE(vault::apply_tag_bundle(dst, std::vector<std::string>{"tree"}, "bundle", true) ==
                Ok);
        REQUIRE(dst.resolve_node("tree")->tags.size() == 1);
        CHECK(dst.resolve_node("tree")->tags[0] == "boundary");
        REQUIRE(vault::transfer_image(src, "parent/tree/child", "file.bin", dst, "",
                                      vault::TransferMode::Copy) == Ok);
        CHECK_EQ(dst.resolve_node("file.bin")->explicit_tags.size(), size_t{2});
        REQUIRE(dst.create_gallery("single") == Ok);
        REQUIRE(vault::save_tag_bundle(src, "own", {"own-tag", "transient", "boundary"}) == Ok);
        REQUIRE(vault::apply_tag_bundle(src, std::vector<std::string>{"parent/tree/child/file.bin"},
                                        "own", false) == Ok);
        REQUIRE(src.add_tag("parent/tree/child/file.bin", "own-tag") == Ok);
        REQUIRE(vault::transfer_image(src, "parent/tree/child", "file.bin", dst, "single",
                                      vault::TransferMode::Copy) == Ok);
        CHECK_EQ(dst.resolve_node("single/file.bin")->tag_bundles.size(), size_t{1});
        REQUIRE(vault::apply_tag_bundle(dst, std::vector<std::string>{"single/file.bin"}, "own",
                                        true) == Ok);
        CHECK_EQ(dst.resolve_node("single/file.bin")->tags.size(), size_t{3});
        crypto::SecureBytes plain;
        REQUIRE(dst.read_image(*dst.resolve_node("single/file.bin"), plain) == Ok);
        CHECK(std::ranges::equal(plain.as_span(), data));
    }
    {
        vault::Vault dst;
        REQUIRE(vaulttest::open_vault(dir / "destination.osv", dst));
        CHECK(dst.resolve_node("tree/child/file.bin")->tags.empty());
        CHECK_EQ(dst.resolve_node("file.bin")->tags.size(), size_t{2});
    }
    std::filesystem::remove_all(dir);
}

TEST(tag_bundles_failed_gallery_move_preserves_source_metadata)
{
    const auto dir = std::filesystem::temp_directory_path() / "osv-bundle-failed-move";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    using enum vault::VaultResult;
    {
        vault::Vault src, dst;
        REQUIRE(vaulttest::make_directory_vault(src, dir / "source.osv"));
        REQUIRE(vaulttest::make_legacy_writable_vault(dst, dir / "destination.osv"));
        REQUIRE(src.create_gallery("tree") == Ok);
        REQUIRE(vault::save_tag_bundle(src, "bundle", {"kept"}) == Ok);
        REQUIRE(vault::apply_tag_bundle(src, std::vector<std::string>{"tree"}, "bundle", false) ==
                Ok);
        CHECK(vault::transfer_gallery(src, "tree", dst, "", vault::TransferMode::Move) ==
              InvalidArg);
        REQUIRE(src.resolve_node("tree"));
        CHECK_EQ(src.resolve_node("tree")->tag_bundles.size(), size_t{1});
    }
    std::filesystem::remove_all(dir);
}

TEST(tag_bundles_combine_preserves_bundles_and_preexisting_manual_assignments)
{
    const auto dir = std::filesystem::temp_directory_path() / "osv-bundle-combine";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    using enum vault::VaultResult;
    {
        vault::Vault src, dst;
        REQUIRE(vaulttest::make_directory_vault(src, dir / "source.osv"));
        REQUIRE(vaulttest::make_directory_vault(dst, dir / "destination.osv"));
        REQUIRE(src.create_gallery("tree") == Ok);
        REQUIRE(dst.create_gallery("tree") == Ok);
        REQUIRE(dst.add_tag("tree", "kept") == Ok);
        REQUIRE(vault::save_tag_bundle(src, "bundle", {"kept", "bundle-only"}) == Ok);
        REQUIRE(vault::apply_tag_bundle(src, std::vector<std::string>{"tree"}, "bundle", false) ==
                Ok);
        const std::array<uint8_t, 4> data{1, 2, 3, 4};
        REQUIRE(src.add_image("tree", data, "file.bin") == Ok);
        REQUIRE(src.create_gallery("tree/child") == Ok);
        REQUIRE(src.add_image("tree/child", data, "nested.bin") == Ok);
        REQUIRE(vault::transfer_gallery(src, "tree", dst, "", vault::TransferMode::Copy,
                                        {.policy = vault::CollisionPolicy::Combine}) == Ok);
        REQUIRE(dst.resolve_node("tree")->tag_bundles.size() == 1);
        REQUIRE(dst.resolve_node("tree/file.bin"));
        CHECK(dst.resolve_node("tree/file.bin")->tags.empty());
        REQUIRE(dst.resolve_node("tree/child/nested.bin"));
        CHECK(dst.resolve_node("tree/child")->tags.empty());
        CHECK(dst.resolve_node("tree/child/nested.bin")->tags.empty());
        REQUIRE(vault::apply_tag_bundle(dst, std::vector<std::string>{"tree"}, "bundle", true) ==
                Ok);
        REQUIRE(dst.resolve_node("tree")->tags.size() == 1);
        CHECK(dst.resolve_node("tree")->tags[0] == "kept");
    }
    std::filesystem::remove_all(dir);
}

TEST(tag_bundles_failed_commit_restores_direct_and_bundle_provenance)
{
    const auto path = std::filesystem::temp_directory_path() / "osv-bundle-rollback.osv";
    std::filesystem::remove_all(path);
    using enum vault::VaultResult;
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, path));
        REQUIRE(v.create_gallery("one") == Ok);
        REQUIRE(v.create_gallery("bad") == Ok);
        REQUIRE(v.add_tag("one", "manual") == Ok);
        REQUIRE(vault::save_tag_bundle(v, "bundle", {"supplied"}) == Ok);
        const std::vector<std::string> paths{"one", "one"};
        REQUIRE(vault::apply_tag_bundle(v, paths, "bundle", false) == Ok);
        auto* invalid = v.resolve_node("bad");
        invalid->name =
            std::string(256, 'x');  // Reject snapshot after live edits, before DB publication.
        CHECK(v.set_tags("one", {}) != Ok);
        CHECK(v.add_tag("one", "supplied") != Ok);
        CHECK(vault::remove_tag_batch(v, paths, "manual") != Ok);
        CHECK(vault::apply_tag_bundle(v, paths, "bundle", true) != Ok);
        CHECK(vault::save_tag_bundle(v, "bundle", {"changed"}) != Ok);
        CHECK(vault::delete_tag_bundle(v, "bundle") != Ok);
        REQUIRE(v.resolve_node("one")->explicit_tags.size() == 1);
        CHECK(v.resolve_node("one")->explicit_tags[0] == "manual");
        CHECK_EQ(v.resolve_node("one")->tag_bundles.size(), size_t{1});
        CHECK_EQ(v.resolve_node("one")->tags.size(), size_t{2});
        REQUIRE(vault::vault_settings(v).tag_bundles.size() == 1);
        CHECK(vault::vault_settings(v).tag_bundles[0].tags[0] == "supplied");
        invalid->name = "bad";
    }
    {
        vault::Vault v;
        REQUIRE(vaulttest::open_vault(path, v));
        CHECK_EQ(v.resolve_node("one")->tags.size(), size_t{2});
        CHECK_EQ(v.resolve_node("one")->explicit_tags.size(), size_t{1});
    }
    std::filesystem::remove_all(path);
}

TEST(tag_bundles_gallery_move_commit_failure_does_not_prune_source)
{
    const auto dir = std::filesystem::temp_directory_path() / "osv-bundle-move-save-failure";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    using enum vault::VaultResult;
    {
        vault::Vault src, dst;
        REQUIRE(vaulttest::make_directory_vault(src, dir / "source.osv"));
        REQUIRE(vaulttest::make_directory_vault(dst, dir / "destination.osv"));
        REQUIRE(src.create_gallery("tree") == Ok);
        REQUIRE(dst.create_gallery("invalid") == Ok);
        REQUIRE(vault::save_tag_bundle(src, "bundle", {"metadata"}) == Ok);
        REQUIRE(vault::apply_tag_bundle(src, std::vector<std::string>{"tree"}, "bundle", false) ==
                Ok);
        dst.resolve_node("invalid")->name = std::string(256, 'x');
        CHECK(vault::transfer_gallery(src, "tree", dst, "", vault::TransferMode::Move) != Ok);
        CHECK(src.resolve_node("tree") != nullptr);
    }
    {
        vault::Vault src;
        REQUIRE(vaulttest::open_vault(dir / "source.osv", src));
        REQUIRE(src.resolve_node("tree"));
        CHECK_EQ(src.resolve_node("tree")->tag_bundles.size(), size_t{1});
    }
    std::filesystem::remove_all(dir);
}

TEST(tag_bundles_normalize_members_like_manual_tags_and_preserve_provenance)
{
    const auto path = std::filesystem::temp_directory_path() / "osv-bundle-normalize.osv";
    std::filesystem::remove_all(path);
    using enum vault::VaultResult;
    const std::vector<std::string> members{" red ",      "RED",      "",      "\t\r\n",
                                           " new york ", "New York", " blue "};
    const std::vector<std::string> paths{"one"};
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, path));
        REQUIRE(v.create_gallery("one") == Ok);
        REQUIRE(v.create_gallery("ordinary") == Ok);
        REQUIRE(v.set_tags("ordinary", members) == Ok);
        REQUIRE(vault::save_tag_bundle(v, "bundle", members) == Ok);
        CHECK(vault::vault_settings(v).tag_bundles[0].tags == v.resolve_node("ordinary")->tags);
        REQUIRE(v.add_tag("one", "red") == Ok);
        REQUIRE(vault::apply_tag_bundle(v, paths, "bundle", false) == Ok);
        REQUIRE(v.add_tag("one", " BLUE ") == Ok);
        REQUIRE(vault::remove_tag_batch(v, paths, " RED ") == Ok);
        CHECK_EQ(v.resolve_node("one")->tags.size(), size_t{3});
        REQUIRE(vault::apply_tag_bundle(v, paths, "bundle", true) == Ok);
        REQUIRE(v.resolve_node("one")->tags.size() == 1);
        CHECK(vault::tag_ci_equal(v.resolve_node("one")->tags[0].view(), "blue"));
    }
    {
        vault::Vault v;
        REQUIRE(vaulttest::open_vault(path, v));
        REQUIRE(vault::vault_settings(v).tag_bundles[0].tags.size() == 3);
        CHECK(vault::vault_settings(v).tag_bundles[0].tags[1] == "new york");
        REQUIRE(vault::apply_tag_bundle(v, paths, "bundle", false) == Ok);
        REQUIRE(v.remove_tag("one", " blue ") == Ok);
        CHECK_EQ(v.resolve_node("one")->tags.size(), size_t{3});
        REQUIRE(vault::save_tag_bundle(v, "bundle", {"", "  ", "\t\n"}) == Ok);
        CHECK(v.resolve_node("one")->tags.empty());
        CHECK(vault::vault_settings(v).tag_bundles[0].tags.empty());
    }
    {
        vault::Vault v;
        REQUIRE(vaulttest::open_vault(path, v));
        CHECK(v.resolve_node("one")->tags.empty());
        CHECK(vault::vault_settings(v).tag_bundles[0].tags.empty());
    }
    std::filesystem::remove_all(path);
}
