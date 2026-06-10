#include "test_framework.h"

#include <filesystem>

#include "platform/paths.h"

TEST(paths_default_vault_filename)
{
    CHECK(platform::default_vault_path().filename() == "vault.osv");
}

TEST(paths_default_vault_under_config_dir)
{
    auto cfg = platform::config_dir();
    if (!cfg.empty())
        CHECK(platform::default_vault_path().parent_path() == cfg);
}

TEST(paths_read_file_roundtrip)
{
    auto tmp = std::filesystem::temp_directory_path() / "osv_paths_test.bin";
    const std::vector<uint8_t> data{1, 2, 3, 4, 250};
    {
        std::FILE* f = std::fopen(tmp.string().c_str(), "wb");
        REQUIRE(f != nullptr);
        std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
    }
    auto got = platform::read_file(tmp);
    REQUIRE(got.has_value());
    CHECK_BYTES_EQ(std::span<const uint8_t>(*got), std::span<const uint8_t>(data));
    std::filesystem::remove(tmp);
}

TEST(paths_read_file_missing_returns_nullopt)
{
    CHECK_FALSE(platform::read_file("/no/such/osv/file.xyz").has_value());
}
