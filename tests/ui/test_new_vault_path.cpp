#include "test_framework.h"
#include "ui/new_vault_path.h"

#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <string>

TEST(new_vault_path_adds_suffix_and_preserves_unicode)
{
    auto result = ui::new_vault_path("/tmp", "Photos été");
    CHECK(result.error == nullptr);
    CHECK(result.path == std::filesystem::path("/tmp/Photos été.osv"));
    CHECK(ui::new_vault_path("/tmp", "Photos.osv").path == "/tmp/Photos.osv");
}

TEST(new_vault_path_rejects_unsafe_or_empty_names)
{
    for (const auto* name : {"", ".", "..", "../outside", "/outside", "a/b", "a\\b", "   "})
        CHECK(ui::new_vault_path("/tmp", name).error != nullptr);
    CHECK(ui::new_vault_path("/tmp", std::string(252, 'a')).error != nullptr);
    CHECK(ui::new_vault_path({}, "Photos").error != nullptr);
}

TEST(new_vault_path_rejects_existing_destination_and_missing_parent)
{
    char pattern[] = "/tmp/osv-new-vault-XXXXXX";
    const auto* created = mkdtemp(pattern);
    REQUIRE(created != nullptr);
    const std::filesystem::path parent(created);
    std::filesystem::create_directory(parent / "directory.osv");
    std::ofstream(parent / "file.osv") << "keep";
    std::filesystem::create_symlink(parent / "missing", parent / "link.osv");
    CHECK(ui::new_vault_path(parent, "directory").error != nullptr);
    CHECK(ui::new_vault_path(parent, "file").error != nullptr);
    CHECK(ui::new_vault_path(parent, "link").error != nullptr);
    CHECK(ui::new_vault_path(parent / "missing", "Photos").error != nullptr);
    CHECK(ui::new_vault_path(parent / "file.osv", "Photos").error != nullptr);
    CHECK(ui::new_vault_path(parent, "Photos").error == nullptr);
    CHECK_FALSE(std::filesystem::exists(parent / "Photos.osv"));
    std::filesystem::remove_all(parent);
}
