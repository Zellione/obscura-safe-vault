#include "test_framework.h"

#include "platform/path_utf8.h"
#include "ui/legacy_conversion_job.h"
#include "vault/vault.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <thread>
#include <unistd.h>

namespace {
struct TempDir {
    std::filesystem::path path;
    TempDir()
    {
        std::array<char, 64> pattern{};
        std::snprintf(pattern.data(), pattern.size(), "/tmp/osv-convert-job-XXXXXX");
        path = ::mkdtemp(pattern.data());
    }
    ~TempDir()
    {
        std::filesystem::remove_all(path);
    }
};
constexpr std::array<uint8_t, 2> SOURCE_PASSWORD{'p', 'w'};
constexpr std::array<uint8_t, 3> DEST_PASSWORD{'n', 'e', 'w'};
constexpr crypto::KdfParams TEST_KDF{1, 8, 1};
}  // namespace

TEST(legacy_conversion_job_replaces_source_handle_only_after_verified_success)
{
    TempDir temp;
    const auto source_path = temp.path / "source.osv";
    const auto destination = temp.path / "converted.osv";
    vault::Vault source;
    REQUIRE(vault::Vault::create(platform::path_to_utf8(source_path), SOURCE_PASSWORD, {}, TEST_KDF,
                                 source) == vault::VaultResult::Ok);
    source.lock();
    REQUIRE(vault::Vault::open(platform::path_to_utf8(source_path), source) ==
            vault::VaultResult::Ok);
    REQUIRE(source.unlock(SOURCE_PASSWORD, {}) == vault::VaultResult::Ok);

    ui::LegacyConversionJob job;
    REQUIRE(job.start(source, destination, DEST_PASSWORD, {}, TEST_KDF));
    std::optional<vault::LegacyConversionReport> result;
    for (int i = 0; i < 500 && !result; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        result = job.take_outcome();
    }
    REQUIRE(result.has_value());
    REQUIRE(result->status == vault::LegacyConversionStatus::Ok);
    CHECK(vault::vault_uses_directory_storage(source));
    CHECK(source.resolve_node("") != nullptr);
}

TEST(legacy_conversion_job_rejects_nonlegacy_source)
{
    TempDir temp;
    vault::Vault source;
    REQUIRE(vault::Vault::create_directory(platform::path_to_utf8(temp.path / "source.osv"),
                                           SOURCE_PASSWORD, {}, TEST_KDF,
                                           source) == vault::VaultResult::Ok);
    ui::LegacyConversionJob job;
    CHECK(!job.start(source, temp.path / "converted.osv", DEST_PASSWORD, {}, TEST_KDF));
}
