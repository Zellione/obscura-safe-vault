#include "test_framework.h"

#include "crypto/kdf.h"
#include "platform/path_utf8.h"
#include "vault/legacy_converter.h"
#include "vault/staging.h"
#include "vault/v3_recovery.h"
#include "vault/vault.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unistd.h>
#include <vector>

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir()
    {
        std::array<char, 64> pattern{};
        std::snprintf(pattern.data(), pattern.size(), "/tmp/osv-convert-XXXXXX");
        path = ::mkdtemp(pattern.data());
    }
    ~TempDir()
    {
        std::filesystem::remove_all(path);
    }
};

std::vector<uint8_t> file_bytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

constexpr std::array<uint8_t, 2> SOURCE_PASSWORD{'p', 'w'};
constexpr std::array<uint8_t, 3> DEST_PASSWORD{'n', 'e', 'w'};
constexpr crypto::KdfParams TEST_KDF{1, 8, 1};

}  // namespace

TEST(legacy_converter_copies_nested_media_metadata_and_preserves_source)
{
    TempDir temp;
    const auto source_path = temp.path / "source.osv";
    const auto destination_path = temp.path / "converted.osv";
    const auto source_utf8 = platform::path_to_utf8(source_path);
    const auto destination_utf8 = platform::path_to_utf8(destination_path);

    vault::Vault writer;
    REQUIRE(vault::Vault::create(source_utf8, SOURCE_PASSWORD, {}, TEST_KDF, writer) ==
            vault::VaultResult::Ok);
    REQUIRE(vault::ensure_gallery_path(writer, "Trips/2024") == vault::VaultResult::Ok);
    const std::array<uint8_t, 7> original{9, 8, 7, 6, 5, 4, 3};
    vault::StagedThumb thumb;
    REQUIRE(thumb.thumb_jpeg.assign(std::array<uint8_t, 4>{1, 2, 3, 4}));
    thumb.format = vault::ImageFormat::JPEG;
    thumb.width = 640;
    thumb.height = 480;
    const vault::NodeExtras extras{{"place:Berlin", "year:2024"}, true};
    REQUIRE(vault::attach_image_prestaged(writer, "Trips/2024", original, "photo.jpg", thumb,
                                          1'700'000'000, &extras) == vault::VaultResult::Ok);
    REQUIRE(vault::set_gallery_sort(writer, "Trips", vault::SortKey::NameDesc) ==
            vault::VaultResult::Ok);
    REQUIRE(vault::commit_staged(writer) == vault::VaultResult::Ok);
    writer.lock();

    const auto source_before = file_bytes(source_path);
    vault::Vault source;
    REQUIRE(vault::Vault::open(source_utf8, source) == vault::VaultResult::Ok);
    REQUIRE(source.unlock(SOURCE_PASSWORD, {}) == vault::VaultResult::Ok);
    REQUIRE(vault::vault_is_read_only(source));

    vault::OpProgress progress;
    vault::LegacyConversionRequest request{destination_path, DEST_PASSWORD, {},
                                           TEST_KDF,         nullptr,       &progress};
    const auto converted = vault::convert_legacy_vault(source, request);
    REQUIRE(converted.status == vault::LegacyConversionStatus::Ok);
    CHECK_EQ(converted.galleries, 3U);
    CHECK_EQ(converted.images, 1U);
    CHECK_EQ(converted.videos, 0U);
    CHECK_EQ(converted.original_bytes, original.size());
    CHECK(converted.deep_verified);
    CHECK(converted.cold_reopened);
    CHECK_EQ(progress.total.load(), 4);
    CHECK_EQ(progress.done.load(), 4);
    CHECK(converted.required_free_bytes >= 32U * 1024U * 1024U);
    CHECK(file_bytes(source_path) == source_before);

    vault::Vault destination;
    REQUIRE(vault::Vault::open(destination_utf8, destination) == vault::VaultResult::Ok);
    REQUIRE(destination.unlock(DEST_PASSWORD, {}) == vault::VaultResult::Ok);
    REQUIRE(vault::vault_uses_directory_storage(destination));
    const auto* copied = destination.resolve_node("Trips/2024/photo.jpg");
    REQUIRE(copied != nullptr);
    CHECK(copied->is_image());
    CHECK(copied->favorite);
    CHECK(copied->tags.size() == 2);
    CHECK(copied->meta.width == 640);
    CHECK(copied->meta.height == 480);
    CHECK(copied->meta.created_ts == 1'700'000'000);
    CHECK(vault::gallery_sort_key(destination, "Trips") == vault::SortKey::NameDesc);
    crypto::SecureBytes copied_original;
    REQUIRE(destination.read_image(*copied, copied_original) == vault::VaultResult::Ok);
    CHECK(std::ranges::equal(copied_original.as_span(), original));
    crypto::SecureBytes copied_thumb;
    REQUIRE(destination.read_thumbnail(*copied, copied_thumb) == vault::VaultResult::Ok);
    CHECK(std::ranges::equal(copied_thumb.as_span(), thumb.thumb_jpeg.as_span()));
    CHECK(vault::verify_directory_vault(destination, vault::v3::VerifyDepth::Deep).status ==
          vault::v3::RecoveryStatus::Ok);
}

TEST(legacy_converter_rejects_nonlegacy_locked_and_alias_destinations)
{
    TempDir temp;
    const auto source_path = temp.path / "source.osv";
    const auto source_utf8 = platform::path_to_utf8(source_path);
    vault::Vault writer;
    REQUIRE(vault::Vault::create(source_utf8, SOURCE_PASSWORD, {}, TEST_KDF, writer) ==
            vault::VaultResult::Ok);
    writer.lock();

    vault::LegacyConversionRequest aliases_source{source_path, DEST_PASSWORD, {}, TEST_KDF};
    CHECK(vault::convert_legacy_vault(writer, aliases_source).status ==
          vault::LegacyConversionStatus::SourceLocked);

    REQUIRE(writer.unlock(SOURCE_PASSWORD, {}) == vault::VaultResult::Ok);
    CHECK(vault::convert_legacy_vault(writer, aliases_source).status ==
          vault::LegacyConversionStatus::InvalidDestination);

    vault::Vault directory;
    const auto directory_path = temp.path / "already-v3.osv";
    REQUIRE(vault::Vault::create_directory(platform::path_to_utf8(directory_path), DEST_PASSWORD,
                                           {}, TEST_KDF, directory) == vault::VaultResult::Ok);
    const vault::LegacyConversionRequest other{
        temp.path / "other.osv", DEST_PASSWORD, {}, TEST_KDF};
    CHECK(vault::convert_legacy_vault(directory, other).status ==
          vault::LegacyConversionStatus::SourceNotLegacy);
}

TEST(legacy_converter_resumes_only_its_matching_incomplete_destination)
{
    TempDir temp;
    const auto source_path = temp.path / "source.osv";
    const auto destination_path = temp.path / "converted.osv";
    vault::Vault writer;
    REQUIRE(vault::Vault::create(platform::path_to_utf8(source_path), SOURCE_PASSWORD, {}, TEST_KDF,
                                 writer) == vault::VaultResult::Ok);
    writer.lock();
    vault::Vault source;
    REQUIRE(vault::Vault::open(platform::path_to_utf8(source_path), source) ==
            vault::VaultResult::Ok);
    REQUIRE(source.unlock(SOURCE_PASSWORD, {}) == vault::VaultResult::Ok);

    std::atomic_bool stop{true};
    vault::LegacyConversionRequest cancelled{destination_path, DEST_PASSWORD, {}, TEST_KDF, &stop};
    CHECK(vault::convert_legacy_vault(source, cancelled).status ==
          vault::LegacyConversionStatus::Cancelled);
    REQUIRE(std::filesystem::is_directory(destination_path));

    stop = false;
    const auto resumed = vault::convert_legacy_vault(source, cancelled);
    CHECK(resumed.status == vault::LegacyConversionStatus::Ok);
    CHECK(resumed.resumed);

    const auto unrelated = temp.path / "unrelated.osv";
    vault::Vault other;
    REQUIRE(vault::Vault::create_directory(platform::path_to_utf8(unrelated), DEST_PASSWORD, {},
                                           TEST_KDF, other) == vault::VaultResult::Ok);
    other.lock();
    vault::LegacyConversionRequest mismatch{unrelated, DEST_PASSWORD, {}, TEST_KDF};
    CHECK(vault::convert_legacy_vault(source, mismatch).status ==
          vault::LegacyConversionStatus::ResumeMismatch);
}

TEST(legacy_converter_streams_multichunk_video_exactly)
{
    TempDir temp;
    const auto source_path = temp.path / "source.osv";
    const auto destination_path = temp.path / "converted.osv";
    const auto video = file_bytes(std::filesystem::path{OSV_VAULT_FIXTURE_DIR} / "tiny.mp4");
    REQUIRE(video.size() > 64);

    vault::Vault writer;
    REQUIRE(vault::Vault::create(platform::path_to_utf8(source_path), SOURCE_PASSWORD, {}, TEST_KDF,
                                 writer) == vault::VaultResult::Ok);
    REQUIRE(writer.add_video("", video, "tiny.mp4", 17) == vault::VaultResult::Ok);
    writer.lock();

    vault::Vault source;
    REQUIRE(vault::Vault::open(platform::path_to_utf8(source_path), source) ==
            vault::VaultResult::Ok);
    REQUIRE(source.unlock(SOURCE_PASSWORD, {}) == vault::VaultResult::Ok);
    const vault::LegacyConversionRequest request{destination_path, DEST_PASSWORD, {}, TEST_KDF};
    const auto report = vault::convert_legacy_vault(source, request);
    REQUIRE(report.status == vault::LegacyConversionStatus::Ok);
    CHECK_EQ(report.videos, 1U);
    CHECK_EQ(report.original_bytes, video.size());

    vault::Vault destination;
    REQUIRE(vault::Vault::open(platform::path_to_utf8(destination_path), destination) ==
            vault::VaultResult::Ok);
    REQUIRE(destination.unlock(DEST_PASSWORD, {}) == vault::VaultResult::Ok);
    const auto* node = destination.resolve_node("tiny.mp4");
    REQUIRE(node != nullptr);
    crypto::SecureBytes copied;
    REQUIRE(destination.read_video(*node, copied) == vault::VaultResult::Ok);
    CHECK(std::ranges::equal(copied.as_span(), video));
}
