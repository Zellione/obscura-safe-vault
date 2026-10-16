#include "test_framework.h"

#include "crypto/kdf.h"
#include "platform/path_utf8.h"
#include "vault/legacy_converter.h"
#include "vault/staging.h"
#include "vault/v3_fs.h"
#include "vault/v3_read_session.h"
#include "vault/v3_recovery.h"
#include "vault/vault.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <tuple>
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
    CHECK(converted.logical_verified);
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

TEST(legacy_converter_persists_stable_node_mapping_for_resume)
{
    TempDir temp;
    const auto source_path = temp.path / "source.osv";
    const auto destination_path = temp.path / "converted.osv";
    vault::Vault writer;
    REQUIRE(vault::Vault::create(platform::path_to_utf8(source_path), SOURCE_PASSWORD, {}, TEST_KDF,
                                 writer) == vault::VaultResult::Ok);
    REQUIRE(vault::ensure_gallery_path(writer, "one/two") == vault::VaultResult::Ok);
    writer.lock();

    vault::Vault source;
    REQUIRE(vault::Vault::open(platform::path_to_utf8(source_path), source) ==
            vault::VaultResult::Ok);
    REQUIRE(source.unlock(SOURCE_PASSWORD, {}) == vault::VaultResult::Ok);
    std::atomic_bool stop{true};
    const vault::LegacyConversionRequest request{
        destination_path, DEST_PASSWORD, {}, TEST_KDF, &stop};
    REQUIRE(vault::convert_legacy_vault(source, request).status ==
            vault::LegacyConversionStatus::Cancelled);

    auto partial = vault::v3::ReadSession::open(destination_path);
    REQUIRE(partial.session);
    REQUIRE(partial.session->unlock(DEST_PASSWORD, {}) == vault::v3::ReadStatus::Ok);
    const auto first = partial.session->root().node_id;
    partial.session->lock();

    stop = false;
    const auto resumed = vault::convert_legacy_vault(source, request);
    REQUIRE(resumed.status == vault::LegacyConversionStatus::Ok);
    REQUIRE(resumed.resumed);
    vault::Vault complete;
    REQUIRE(vault::Vault::open(platform::path_to_utf8(destination_path), complete) ==
            vault::VaultResult::Ok);
    REQUIRE(complete.unlock(DEST_PASSWORD, {}) == vault::VaultResult::Ok);
    const auto* complete_root = complete.resolve_node("");
    REQUIRE(complete_root != nullptr);
    CHECK(complete_root->node_id == first);
}

TEST(legacy_converter_wrong_resume_credentials_are_authentication_failure)
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
    const vault::LegacyConversionRequest create{
        destination_path, DEST_PASSWORD, {}, TEST_KDF, &stop};
    REQUIRE(vault::convert_legacy_vault(source, create).status ==
            vault::LegacyConversionStatus::Cancelled);
    constexpr std::array<uint8_t, 3> wrong{'b', 'a', 'd'};
    const vault::LegacyConversionRequest resume{destination_path, wrong, {}, TEST_KDF};
    CHECK(vault::convert_legacy_vault(source, resume).status ==
          vault::LegacyConversionStatus::AuthenticationFailed);
}

TEST(legacy_converter_publication_faults_safely_restart_or_resume)
{
    using enum vault::v3::FsFault;
    for (const auto fault : {Write, FileSync, Rename, DirectorySync}) {
        TempDir temp;
        const auto source_path = temp.path / "source.osv";
        const auto destination_path = temp.path / "converted.osv";
        vault::Vault writer;
        REQUIRE(vault::Vault::create(platform::path_to_utf8(source_path), SOURCE_PASSWORD, {},
                                     TEST_KDF, writer) == vault::VaultResult::Ok);
        vault::StagedThumb thumb;
        REQUIRE(thumb.thumb_jpeg.assign(std::array<uint8_t, 2>{8, 9}));
        thumb.format = vault::ImageFormat::JPEG;
        REQUIRE(vault::attach_image_prestaged(writer, "", std::array<uint8_t, 4>{1, 2, 3, 4},
                                              "one.jpg", thumb, 1,
                                              nullptr) == vault::VaultResult::Ok);
        REQUIRE(vault::commit_staged(writer) == vault::VaultResult::Ok);
        writer.lock();
        const auto before = file_bytes(source_path);
        vault::Vault source;
        REQUIRE(vault::Vault::open(platform::path_to_utf8(source_path), source) ==
                vault::VaultResult::Ok);
        REQUIRE(source.unlock(SOURCE_PASSWORD, {}) == vault::VaultResult::Ok);
        const vault::LegacyConversionRequest request{destination_path, DEST_PASSWORD, {}, TEST_KDF};
        vault::v3::inject_fs_fault(fault);
        CHECK(vault::convert_legacy_vault(source, request).status !=
              vault::LegacyConversionStatus::Ok);
        vault::v3::clear_fs_faults();
        const auto retried = vault::convert_legacy_vault(source, request);
        REQUIRE(retried.status == vault::LegacyConversionStatus::Ok);
        CHECK(file_bytes(source_path) == before);
    }
}

TEST(legacy_converter_converts_committed_preframing_fixture_byte_exact)
{
    TempDir temp;
    const auto fixture = std::filesystem::path{OSV_VAULT_FIXTURE_DIR} / "legacy_noflags.osv";
    const auto source_path = temp.path / "legacy.osv";
    const auto destination_path = temp.path / "converted.osv";
    REQUIRE(std::filesystem::copy_file(fixture, source_path));
    const auto before = file_bytes(source_path);
    constexpr std::string_view password = "legacy-password";
    const auto password_bytes = std::span<const uint8_t>{
        reinterpret_cast<const uint8_t*>(password.data()), password.size()};
    vault::Vault source;
    REQUIRE(vault::Vault::open(platform::path_to_utf8(source_path), source) ==
            vault::VaultResult::Ok);
    REQUIRE(source.unlock(password_bytes, {}) == vault::VaultResult::Ok);
    const vault::LegacyConversionRequest request{destination_path, DEST_PASSWORD, {}, TEST_KDF};
    const auto report = vault::convert_legacy_vault(source, request);
    REQUIRE(report.status == vault::LegacyConversionStatus::Ok);
    CHECK(report.logical_verified);
    CHECK_EQ(report.images, 2U);
    CHECK(file_bytes(source_path) == before);

    vault::Vault converted;
    REQUIRE(vault::Vault::open(platform::path_to_utf8(destination_path), converted) ==
            vault::VaultResult::Ok);
    REQUIRE(converted.unlock(DEST_PASSWORD, {}) == vault::VaultResult::Ok);
    for (const auto& [name, size, seed] : {std::tuple{"a.bin", size_t{4096}, uint8_t{0xA5}},
                                           std::tuple{"b.bin", size_t{100000}, uint8_t{0x5A}}}) {
        const auto* node = converted.resolve_node(std::string{"pics/"} + name);
        REQUIRE(node != nullptr);
        crypto::SecureBytes bytes;
        REQUIRE(converted.read_image(*node, bytes) == vault::VaultResult::Ok);
        REQUIRE(bytes.size() == size);
        for (size_t i = 0; i < size; ++i)
            REQUIRE(bytes[i] == static_cast<uint8_t>(seed + i * 31));
    }
}

TEST(legacy_converter_tampered_source_never_claims_success)
{
    TempDir temp;
    const auto source_path = temp.path / "source.osv";
    const auto destination_path = temp.path / "converted.osv";
    vault::Vault writer;
    REQUIRE(vault::Vault::create(platform::path_to_utf8(source_path), SOURCE_PASSWORD, {}, TEST_KDF,
                                 writer) == vault::VaultResult::Ok);
    vault::StagedThumb thumb;
    thumb.format = vault::ImageFormat::JPEG;
    REQUIRE(vault::attach_image_prestaged(writer, "", std::array<uint8_t, 4>{1, 2, 3, 4}, "one.jpg",
                                          thumb, 1, nullptr) == vault::VaultResult::Ok);
    REQUIRE(vault::commit_staged(writer) == vault::VaultResult::Ok);
    const auto* node = writer.resolve_node("one.jpg");
    REQUIRE(node != nullptr);
    const auto offset = node->meta.data_offset + 30;
    writer.lock();
    {
        std::fstream file(source_path, std::ios::binary | std::ios::in | std::ios::out);
        REQUIRE(file.good());
        file.seekg(static_cast<std::streamoff>(offset));
        char byte = 0;
        file.read(&byte, 1);
        byte ^= 0x40;
        file.seekp(static_cast<std::streamoff>(offset));
        file.write(&byte, 1);
    }
    const auto tampered = file_bytes(source_path);
    vault::Vault source;
    REQUIRE(vault::Vault::open(platform::path_to_utf8(source_path), source) ==
            vault::VaultResult::Ok);
    REQUIRE(source.unlock(SOURCE_PASSWORD, {}) == vault::VaultResult::Ok);
    const vault::LegacyConversionRequest request{destination_path, DEST_PASSWORD, {}, TEST_KDF};
    const auto report = vault::convert_legacy_vault(source, request);
    CHECK(report.status == vault::LegacyConversionStatus::SourceCorrupt);
    CHECK(report.failed_ordinal != 0);
    CHECK(!report.logical_verified);
    CHECK(file_bytes(source_path) == tampered);
}

TEST(legacy_converter_space_budget_does_not_reserve_a_second_copy)
{
    TempDir temp;
    const auto source_path = temp.path / "source.osv";
    vault::Vault writer;
    REQUIRE(vault::Vault::create(platform::path_to_utf8(source_path), SOURCE_PASSWORD, {}, TEST_KDF,
                                 writer) == vault::VaultResult::Ok);
    writer.lock();
    // Sparse padding models a large source without allocating or copying its dead bytes.
    constexpr uint64_t SOURCE_SIZE = 512ULL * 1024 * 1024;
    std::filesystem::resize_file(source_path, SOURCE_SIZE);
    vault::Vault source;
    REQUIRE(vault::Vault::open(platform::path_to_utf8(source_path), source) ==
            vault::VaultResult::Ok);
    REQUIRE(source.unlock(SOURCE_PASSWORD, {}) == vault::VaultResult::Ok);
    const vault::LegacyConversionRequest request{
        temp.path / "converted.osv", DEST_PASSWORD, {}, TEST_KDF};
    const auto report = vault::convert_legacy_vault(source, request);
    REQUIRE(report.status == vault::LegacyConversionStatus::Ok);
    CHECK(report.required_free_bytes >= SOURCE_SIZE);
    CHECK(report.required_free_bytes < SOURCE_SIZE + SOURCE_SIZE / 10 + 32ULL * 1024 * 1024);
    CHECK_EQ(std::filesystem::file_size(source_path), SOURCE_SIZE);
}

TEST(legacy_converter_space_budget_accounts_for_expanded_media)
{
    TempDir temp;
    const auto source_path = temp.path / "source.osv";
    vault::Vault writer;
    REQUIRE(vault::Vault::create(platform::path_to_utf8(source_path), SOURCE_PASSWORD, {}, TEST_KDF,
                                 writer) == vault::VaultResult::Ok);
    const std::vector<uint8_t> original(4U * 1024 * 1024, 7);
    vault::StagedThumb thumb;
    REQUIRE(thumb.thumb_jpeg.assign(std::array<uint8_t, 4>{1, 2, 3, 4}));
    thumb.format = vault::ImageFormat::JPEG;
    REQUIRE(vault::attach_image_prestaged(writer, "", original, "photo.jpg", thumb, 0) ==
            vault::VaultResult::Ok);
    REQUIRE(vault::commit_staged(writer) == vault::VaultResult::Ok);
    writer.lock();
    REQUIRE(std::filesystem::file_size(source_path) < original.size());
    vault::Vault source;
    REQUIRE(vault::Vault::open(platform::path_to_utf8(source_path), source) ==
            vault::VaultResult::Ok);
    REQUIRE(source.unlock(SOURCE_PASSWORD, {}) == vault::VaultResult::Ok);
    const vault::LegacyConversionRequest request{
        temp.path / "converted.osv", DEST_PASSWORD, {}, TEST_KDF};
    const auto report = vault::convert_legacy_vault(source, request);
    REQUIRE(report.status == vault::LegacyConversionStatus::Ok);
    constexpr uint64_t SLACK = 32ULL * 1024 * 1024;
    CHECK(report.required_free_bytes > original.size() + SLACK);
    CHECK(report.required_free_bytes < original.size() + original.size() / 10 + SLACK);
}

TEST(legacy_converter_space_budget_rejects_overflow_before_creating_destination)
{
    TempDir temp;
    const auto source_path = temp.path / "source.osv";
    vault::Vault source;
    REQUIRE(vault::Vault::create(platform::path_to_utf8(source_path), SOURCE_PASSWORD, {}, TEST_KDF,
                                 source) == vault::VaultResult::Ok);
    vault::StagedThumb thumb;
    const std::array<uint8_t, 1> original{7};
    REQUIRE(vault::attach_image_prestaged(source, "", original, "photo.jpg", thumb, 0) ==
            vault::VaultResult::Ok);
    auto* node = source.resolve_node("photo.jpg");
    REQUIRE(node != nullptr);
    const vault::LegacyConversionRequest request{
        temp.path / "converted.osv", DEST_PASSWORD, {}, TEST_KDF};
    // Exercise both scan accumulation and the added percentage overflowing.
    for (const uint64_t derived : {uint64_t{0}, uint64_t{1}}) {
        node->meta.orig_size = std::numeric_limits<uint64_t>::max();
        node->meta.thumb_length = derived;
        const auto report = vault::convert_legacy_vault(source, request);
        CHECK(report.status == vault::LegacyConversionStatus::InsufficientSpace);
        CHECK_EQ(report.required_free_bytes, 0U);
        CHECK(!std::filesystem::exists(request.destination));
    }
}

namespace vault {
void test_only_conversion_checkpoint(std::function<void(bool, uint64_t)> callback);
}

TEST(legacy_converter_reports_destination_object_corruption)
{
    const std::array reasons{
        "Referenced encrypted object is missing", "Encrypted object size differs from database",
        "Encrypted object authentication failed", "Encrypted object has malformed framing"};
    for (size_t mode = 0; mode < reasons.size(); ++mode) {
        TempDir temp;
        vault::Vault source;
        REQUIRE(vault::Vault::create(platform::path_to_utf8(temp.path / "source.osv"),
                                     SOURCE_PASSWORD, {}, TEST_KDF,
                                     source) == vault::VaultResult::Ok);
        const std::array<uint8_t, 7> original{9, 8, 7, 6, 5, 4, 3};
        vault::StagedThumb thumb;
        thumb.format = vault::ImageFormat::JPEG;
        REQUIRE(vault::attach_image_prestaged(source, "", original, "photo", thumb, 0) ==
                vault::VaultResult::Ok);
        REQUIRE(vault::commit_staged(source) == vault::VaultResult::Ok);
        const auto destination = temp.path / "converted.osv";
        bool removed = false;
        vault::test_only_conversion_checkpoint([&](bool verifying, uint64_t) {
            if (!verifying) return;
            for (const auto& entry :
                 std::filesystem::recursive_directory_iterator(destination / "objects")) {
                if (entry.is_regular_file()) {
                    if (mode == 0) {
                        removed = std::filesystem::remove(entry.path());
                    } else if (mode == 1) {
                        std::filesystem::resize_file(entry.path(), entry.file_size() - 1);
                        removed = true;
                    } else {
                        std::fstream file(entry.path(),
                                          std::ios::binary | std::ios::in | std::ios::out);
                        const auto offset =
                            mode == 2 ? static_cast<std::streamoff>(entry.file_size() - 1) : 0;
                        file.seekg(offset);
                        char byte = 0;
                        file.read(&byte, 1);
                        byte ^= 1;
                        file.seekp(offset);
                        file.write(&byte, 1);
                        removed = file.good();
                    }
                    break;
                }
            }
        });
        const auto result =
            vault::convert_legacy_vault(source, {destination, DEST_PASSWORD, {}, TEST_KDF});
        vault::test_only_conversion_checkpoint({});
        REQUIRE(removed);
        CHECK(result.status == vault::LegacyConversionStatus::VerificationFailed);
        CHECK(std::string_view(result.operation) == "Verify written destination");
        CHECK(std::string_view(result.detail) == reasons[mode]);
        CHECK(std::ranges::any_of(result.failed_object, [](uint8_t byte) { return byte != 0; }));
        CHECK_FALSE(result.deep_verified);
        CHECK_FALSE(result.logical_verified);
    }
}

TEST(legacy_converter_reports_metadata_field_and_source_ordinal)
{
    TempDir temp;
    vault::Vault source;
    REQUIRE(vault::Vault::create(platform::path_to_utf8(temp.path / "source.osv"), SOURCE_PASSWORD,
                                 {}, TEST_KDF, source) == vault::VaultResult::Ok);
    const std::array<uint8_t, 7> original{9, 8, 7, 6, 5, 4, 3};
    vault::StagedThumb thumb;
    thumb.format = vault::ImageFormat::JPEG;
    thumb.width = 640;
    REQUIRE(vault::attach_image_prestaged(source, "", original, "private-name", thumb, 0) ==
            vault::VaultResult::Ok);
    REQUIRE(vault::commit_staged(source) == vault::VaultResult::Ok);
    vault::test_only_conversion_checkpoint([&](bool verifying, uint64_t) {
        if (verifying) source.resolve_node("private-name")->meta.width = 641;
    });
    const auto result = vault::convert_legacy_vault(
        source, {temp.path / "converted.osv", DEST_PASSWORD, {}, TEST_KDF});
    vault::test_only_conversion_checkpoint({});
    CHECK(result.status == vault::LegacyConversionStatus::VerificationFailed);
    CHECK(std::string_view(result.operation) == "Compare node metadata");
    CHECK(std::string_view(result.detail) == "Image width differs");
    CHECK_EQ(result.failed_ordinal, 2U);
    CHECK(result.deep_verified);
    CHECK(result.cold_reopened);
    CHECK_FALSE(result.logical_verified);
}

TEST(legacy_converter_video_write_failure_does_not_blame_source)
{
    TempDir temp;
    vault::Vault source;
    REQUIRE(vault::Vault::create(platform::path_to_utf8(temp.path / "source.osv"), SOURCE_PASSWORD,
                                 {}, TEST_KDF, source) == vault::VaultResult::Ok);
    const auto video = file_bytes(std::filesystem::path{OSV_VAULT_FIXTURE_DIR} / "tiny.mp4");
    REQUIRE(source.add_video("", video, "video", 17) == vault::VaultResult::Ok);
    const auto* node = source.resolve_node("video");
    REQUIRE(node != nullptr);
    auto destination =
        vault::v3::ReadSession::create(temp.path / "converted.osv", DEST_PASSWORD, {}, TEST_KDF);
    REQUIRE(destination.status == vault::v3::ReadStatus::Ok);
    REQUIRE(destination.session != nullptr);
    vault::v3::inject_fs_fault(vault::v3::FsFault::FileSync);
    const auto status = vault::copy_legacy_video_object(source, *node, *destination.session, *node);
    vault::v3::clear_fs_faults();
    CHECK(status == vault::LegacyConversionStatus::IoError);
    crypto::SecureBytes original;
    REQUIRE(source.read_video(*node, original) == vault::VaultResult::Ok);
    CHECK(std::ranges::equal(original.as_span(), video));
}

namespace vault {
void test_only_conversion_backend_fault(std::string_view operation, int status);
}

TEST(legacy_converter_preserves_backend_failures_at_operation_boundaries)
{
    struct Failure {
        const char* operation;
        int status;
        const char* reason;
    };
    const auto code = [](auto status) { return static_cast<int>(std::to_underlying(status)); };
    const std::array failures{
        Failure{"Create destination vault and conversion marker", code(vault::VaultResult::IoError),
                "sync"},
        Failure{"Create destination vault and conversion marker",
                code(vault::VaultResult::BadFormat), "format"},
        Failure{"Create destination vault and conversion marker",
                code(vault::VaultResult::AuthFailed), "authentication"},
        Failure{"Create destination vault and conversion marker", code(vault::VaultResult::Locked),
                "locked"},
        Failure{"Create destination vault and conversion marker",
                code(vault::VaultResult::NotFound), "missing"},
        Failure{"Create destination vault and conversion marker",
                code(vault::VaultResult::AlreadyExists), "exists"},
        Failure{"Create destination vault and conversion marker",
                code(vault::VaultResult::InvalidArg), "invalid"},
        Failure{"Create destination vault and conversion marker",
                code(vault::VaultResult::CryptoError), "derivation"},
        Failure{"Create destination vault and conversion marker", code(vault::VaultResult::Busy),
                "lease"},
        Failure{"Create destination vault and conversion marker",
                code(vault::VaultResult::IncompleteConversion), "incomplete"},
        Failure{"Create destination vault and conversion marker", code(vault::VaultResult::Damaged),
                "damaged"},
        Failure{"Create destination vault and conversion marker",
                code(vault::VaultResult::UnsupportedVersion), "unsupported"},
        Failure{"Write destination image", code(vault::v3::ReadStatus::IoError), "sync"},
        Failure{"Write destination image", code(vault::v3::ReadStatus::BadFormat), "format"},
        Failure{"Write destination image", code(vault::v3::ReadStatus::AuthenticationFailed),
                "authentication"},
        Failure{"Write destination image", code(vault::v3::ReadStatus::Busy), "lease"},
        Failure{"Write destination image", code(vault::v3::ReadStatus::UnsupportedVersion),
                "unsupported"},
        Failure{"Write destination image", code(vault::v3::ReadStatus::AlreadyExists), "exists"},
        Failure{"Save resume node mapping", code(vault::v3::DbStatus::InvalidArgument), "invalid"},
        Failure{"Save resume node mapping", code(vault::v3::DbStatus::IoError), "sync"},
        Failure{"Save resume node mapping", code(vault::v3::DbStatus::WrongKeyOrCorrupt),
                "integrity"},
        Failure{"Save resume node mapping", code(vault::v3::DbStatus::Busy), "busy"},
        Failure{"Save resume node mapping", code(vault::v3::DbStatus::DiskFull), "disk is full"},
        Failure{"Save resume node mapping", code(vault::v3::DbStatus::Constraint), "constraint"},
        Failure{"Save resume node mapping", code(vault::v3::DbStatus::UnsupportedVersion),
                "unsupported"},
    };
    for (const auto& failure : failures) {
        TempDir temp;
        vault::Vault source;
        REQUIRE(vault::Vault::create(platform::path_to_utf8(temp.path / "source.osv"),
                                     SOURCE_PASSWORD, {}, TEST_KDF,
                                     source) == vault::VaultResult::Ok);
        vault::StagedThumb thumb;
        thumb.format = vault::ImageFormat::JPEG;
        REQUIRE(vault::attach_image_prestaged(source, "", std::array<uint8_t, 3>{1, 2, 3},
                                              "secret-name", thumb, 0) == vault::VaultResult::Ok);
        REQUIRE(vault::commit_staged(source) == vault::VaultResult::Ok);
        const auto before = file_bytes(temp.path / "source.osv");
        vault::test_only_conversion_backend_fault(failure.operation, failure.status);
        const auto result = vault::convert_legacy_vault(
            source, {temp.path / "converted.osv", DEST_PASSWORD, {}, TEST_KDF});
        vault::test_only_conversion_backend_fault({}, 0);
        CHECK(result.status != vault::LegacyConversionStatus::Ok);
        CHECK(std::string_view(result.operation) == failure.operation);
        CHECK_EQ(result.backend_code, failure.status);
        CHECK(std::string_view(result.detail).find(failure.reason) != std::string_view::npos);
        CHECK(std::string_view(result.detail).find("secret-name") == std::string_view::npos);
        CHECK(file_bytes(temp.path / "source.osv") == before);
    }
}

TEST(legacy_converter_identifies_individual_metadata_differences)
{
    struct Difference {
        bool video;
        void (*change)(vault::IndexNode&);
        const char* reason;
    };
    const std::array differences{
        Difference{false, [](auto& n) { n.type = vault::IndexNode::Type::Gallery; }, "type"},
        Difference{false, [](auto& n) { n.name = "changed"; }, "name"},
        Difference{false, [](auto& n) { n.tags.emplace_back("changed"); }, "tags"},
        Difference{false, [](auto& n) { n.favorite = !n.favorite; }, "favorite"},
        Difference{false, [](auto& n) { n.sort_key = vault::SortKey::NameDesc; }, "sort order"},
        Difference{false, [](auto& n) { n.children.emplace_back(); }, "children count"},
        Difference{false, [](auto& n) { n.meta.format = vault::ImageFormat::PNG; }, "format"},
        Difference{false, [](auto& n) { ++n.meta.width; }, "width"},
        Difference{false, [](auto& n) { ++n.meta.height; }, "height"},
        Difference{false, [](auto& n) { ++n.meta.orig_size; }, "original size"},
        Difference{false, [](auto& n) { ++n.meta.created_ts; }, "creation timestamp"},
        Difference{false, [](auto& n) { n.meta.animated = !n.meta.animated; }, "animated"},
        Difference{true, [](auto& n) { n.vmeta.container = vault::VideoContainer::Unknown; },
                   "container"},
        Difference{true, [](auto& n) { n.vmeta.codec = static_cast<vault::VideoCodec>(99); },
                   "codec"},
        Difference{true, [](auto& n) { ++n.vmeta.width; }, "width"},
        Difference{true, [](auto& n) { ++n.vmeta.height; }, "height"},
        Difference{true, [](auto& n) { ++n.vmeta.duration_us; }, "duration"},
        Difference{true, [](auto& n) { ++n.vmeta.orig_size; }, "original size"},
        Difference{true, [](auto& n) { ++n.vmeta.created_ts; }, "creation timestamp"},
    };
    for (const auto& difference : differences) {
        TempDir temp;
        vault::Vault source;
        REQUIRE(vault::Vault::create(platform::path_to_utf8(temp.path / "source.osv"),
                                     SOURCE_PASSWORD, {}, TEST_KDF,
                                     source) == vault::VaultResult::Ok);
        if (difference.video) {
            const auto video =
                file_bytes(std::filesystem::path{OSV_VAULT_FIXTURE_DIR} / "tiny.mp4");
            REQUIRE(source.add_video("", video, "item", 17) == vault::VaultResult::Ok);
        } else {
            vault::StagedThumb thumb;
            thumb.format = vault::ImageFormat::JPEG;
            REQUIRE(vault::attach_image_prestaged(source, "", std::array<uint8_t, 3>{1, 2, 3},
                                                  "item", thumb, 0) == vault::VaultResult::Ok);
            REQUIRE(vault::commit_staged(source) == vault::VaultResult::Ok);
        }
        auto* node = source.resolve_node("item");
        REQUIRE(node != nullptr);
        vault::test_only_conversion_checkpoint([&difference, node](bool verifying, uint64_t) {
            if (verifying) difference.change(*node);
        });
        const auto result = vault::convert_legacy_vault(
            source, {temp.path / "converted.osv", DEST_PASSWORD, {}, TEST_KDF});
        vault::test_only_conversion_checkpoint({});
        CHECK(result.status == vault::LegacyConversionStatus::VerificationFailed);
        CHECK(result.cold_reopened);
        CHECK_FALSE(result.logical_verified);
        CHECK_EQ(result.failed_ordinal, 2U);
        CHECK(std::string_view(result.detail).find(difference.reason) != std::string_view::npos);
    }
}

TEST(legacy_converter_identifies_settings_differences_after_reopen)
{
    struct Difference {
        void (*change)(vault::VaultSettings&);
        const char* reason;
    };
    const std::array differences{
        Difference{[](auto& s) { s.default_sort = vault::SortKey::NameDesc; }, "default sort"},
        Difference{[](auto& s) { s.tiles_show_tags = !s.tiles_show_tags; }, "tiles show tags"},
        Difference{[](auto& s) { s.categories.clear(); }, "categories"},
        Difference{[](auto& s) { s.tag_descriptions.emplace_back(); }, "tag descriptions"},
        Difference{[](auto& s) { s.tag_field_values.emplace_back(); }, "tag field values"},
        Difference{[](auto& s) { ++s.migrated_index_version; }, "migrated index version"},
        Difference{[](auto& s) { ++s.migrated_probe_caps; }, "migrated probe caps"},
        Difference{[](auto& s) { ++s.migrated_thumb_side; }, "migrated thumb side"},
    };
    for (const auto& difference : differences) {
        TempDir temp;
        vault::Vault source;
        REQUIRE(vault::Vault::create(platform::path_to_utf8(temp.path / "source.osv"),
                                     SOURCE_PASSWORD, {}, TEST_KDF,
                                     source) == vault::VaultResult::Ok);
        vault::test_only_conversion_checkpoint([&source, &difference](bool verifying, uint64_t) {
            // Change only the in-memory comparison input after copying. The source file
            // remains unchanged, and the independently reopened destination stays valid.
            if (verifying)
                difference.change(const_cast<vault::VaultSettings&>(vault::vault_settings(source)));
        });
        const auto result = vault::convert_legacy_vault(
            source, {temp.path / "converted.osv", DEST_PASSWORD, {}, TEST_KDF});
        vault::test_only_conversion_checkpoint({});
        CHECK(result.status == vault::LegacyConversionStatus::VerificationFailed);
        CHECK(result.cold_reopened);
        CHECK_FALSE(result.logical_verified);
        CHECK(std::string_view(result.operation) == "Compare vault settings");
        CHECK(std::string_view(result.detail).find(difference.reason) != std::string_view::npos);
    }
}
