#include "test_framework.h"

#include "ui/zip_test_helpers.h"
#include "vault/fixtures.h"

TEST(vault_fixtures_distinguish_directory_and_legacy_access)
{
    const auto dir = ziptest::fresh_dir("osv_fixture_backends");
    {
        vault::Vault directory, legacy;
        REQUIRE(vaulttest::make_directory_vault(directory, dir / "directory.osv"));
        REQUIRE(vaulttest::make_legacy_writable_vault(legacy, dir / "legacy.osv"));
        CHECK(vault::vault_uses_directory_storage(directory));
        CHECK(!vault::vault_is_read_only(directory));
        CHECK(!vault::vault_uses_directory_storage(legacy));
        CHECK(!vault::vault_is_read_only(legacy));
        REQUIRE(legacy.create_gallery("album") == vault::VaultResult::Ok);
    }
    {
        vault::Vault legacy;
        REQUIRE(vaulttest::open_legacy_read_only_vault(dir / "legacy.osv", legacy));
        CHECK(vault::vault_is_read_only(legacy));
        CHECK(legacy.resolve_node("album") != nullptr);
        CHECK(legacy.create_gallery("rejected") == vault::VaultResult::InvalidArg);
    }
    {
        vault::Vault wrong_backend;
        CHECK(!vaulttest::open_legacy_read_only_vault(dir / "directory.osv", wrong_backend));
    }
    ziptest::cleanup_dir(dir);
}

TEST(vault_fixtures_report_setup_failure)
{
    const auto dir = ziptest::fresh_dir("osv_fixture_setup_failure");
    {
        vault::Vault directory, legacy;
        // An occupied directory cannot become either a new directory vault or a file.
        CHECK(!vaulttest::make_directory_vault(directory, dir));
        CHECK(!vaulttest::make_legacy_writable_vault(legacy, dir));
        CHECK(!vaulttest::open_legacy_read_only_vault(dir / "missing.osv", legacy));
    }
    ziptest::cleanup_dir(dir);
}
