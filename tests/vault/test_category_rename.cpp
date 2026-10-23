#include "test_framework.h"
#include "ui/advanced_search_model.h"
#include "vault/file_util.h"
#include "vault/fixtures.h"
#include "vault/vault_search.h"

TEST(category_rename_updates_assignments_metadata_and_queries_atomically)
{
    const auto path = std::filesystem::temp_directory_path() / "osv-category-rename.osv";
    std::filesystem::remove_all(path);
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, path));
        REQUIRE(v.create_gallery("gallery") == vault::VaultResult::Ok);
        REQUIRE(v.set_tags("", {"PERSON:Alice", "personality:Alice"}) == vault::VaultResult::Ok);
        REQUIRE(v.set_tags("gallery", {"person:Bob"}) == vault::VaultResult::Ok);
        auto settings = vault::vault_settings(v);
        settings.categories.push_back(
            {crypto::SecureString("person"), 2, {crypto::SecureString("year")}});
        vault::set_tag_description(settings, "person:unused", "person:Alice is free text");
        vault::set_tag_field_value(settings, "person:Bob", "year", "2026");
        REQUIRE(vault::set_vault_settings(v, settings) == vault::VaultResult::Ok);
        ui::AdvancedQuery query;
        query.include.push_back({"person:Alice", 2});
        query.exclude.push_back("PERSON:Bob");
        query.groups.push_back({"person", ui::Combinator::Or, {"person:unused"}});
        query.name_query = "person:Alice";
        REQUIRE(vault::VaultSearch(v).save_search("person", query) == vault::VaultResult::Ok);
        REQUIRE(vault::rename_tag_category(v, "PERSON", "people") == vault::VaultResult::Ok);
        CHECK(v.resolve_node("")->tags[0] == "people:Alice");
        CHECK(v.resolve_node("")->tags[1] == "personality:Alice");
        CHECK(v.resolve_node("gallery")->tags[0] == "people:Bob");
        CHECK(vault::find_tag_description(vault::vault_settings(v), "people:unused") ==
              "person:Alice is free text");
        CHECK(vault::find_tag_field_value(vault::vault_settings(v), "people:Bob", "year") ==
              "2026");
        auto saved = vault::VaultSearch(v).list_saved_searches();
        REQUIRE(ui::deserialize_query(saved.front().query.as_span(), query));
        CHECK(query.include[0].tag == "people:Alice");
        CHECK(query.exclude[0] == "people:Bob");
        CHECK(query.groups[0].tags[0] == "people:unused");
        CHECK(query.groups[0].name == "person");
        CHECK(query.name_query == "person:Alice");
        v.lock();
        REQUIRE(v.unlock(vaulttest::kPassword, {}) == vault::VaultResult::Ok);
        CHECK(v.resolve_node("gallery")->tags[0] == "people:Bob");
        REQUIRE(vault::rename_tag_category(v, "people", "PEOPLE") == vault::VaultResult::Ok);
        CHECK(v.resolve_node("gallery")->tags[0] == "PEOPLE:Bob");
    }
    std::filesystem::remove_all(path);
}

TEST(category_rename_rejects_target_namespace_and_unsafe_name)
{
    const auto path = std::filesystem::temp_directory_path() / "osv-category-collision.osv";
    std::filesystem::remove_all(path);
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_directory_vault(v, path));
        auto settings = vault::vault_settings(v);
        settings.categories.push_back({crypto::SecureString("person"), 0, {}});
        vault::set_tag_description(settings, "people:reserved", "metadata only");
        REQUIRE(vault::set_vault_settings(v, settings) == vault::VaultResult::Ok);
        CHECK(vault::rename_tag_category(v, "person", "people") ==
              vault::VaultResult::AlreadyExists);
        CHECK(vault::rename_tag_category(v, "person", "bad:name") ==
              vault::VaultResult::InvalidArg);
        CHECK(vault::rename_tag_category(v, "missing", "new") == vault::VaultResult::NotFound);
    }
    std::filesystem::remove_all(path);
}

TEST(category_rename_commit_failure_restores_category_tags_and_descriptions)
{
    const auto path = std::filesystem::temp_directory_path() / "osv-category-failure.osv";
    std::filesystem::remove_all(path);
    {
        vault::Vault v;
        REQUIRE(vaulttest::make_legacy_writable_vault(v, path));
        REQUIRE(v.set_tags("", {"person:Alice"}) == vault::VaultResult::Ok);
        auto settings = vault::vault_settings(v);
        settings.categories.push_back({crypto::SecureString("person"), 0, {}});
        vault::set_tag_description(settings, "person:Alice", "Description");
        REQUIRE(vault::set_vault_settings(v, settings) == vault::VaultResult::Ok);
        vault::fileutil::inject_sync_failure(0);
        const auto result = vault::rename_tag_category(v, "person", "people");
        vault::fileutil::clear_sync_failure();
        CHECK(result == vault::VaultResult::IoError);
        CHECK(v.resolve_node("")->tags[0] == "person:Alice");
        CHECK(vault::vault_settings(v).categories.back().name == "person");
        CHECK(vault::find_tag_description(vault::vault_settings(v), "person:Alice") ==
              "Description");
        v.lock();
        vault::Vault readonly;
        REQUIRE(vaulttest::open_legacy_read_only_vault(path, readonly));
        CHECK(vault::rename_tag_category(readonly, "person", "people") ==
              vault::VaultResult::InvalidArg);
        CHECK(readonly.resolve_node("")->tags[0] == "person:Alice");
    }
    std::filesystem::remove_all(path);
}
