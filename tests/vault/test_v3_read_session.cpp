#include "test_framework.h"

#include "crypto/aead.h"
#include "crypto/kdf.h"
#include "crypto/random.h"
#include "media/video_source.h"
#include "vault/staging.h"
#include "vault/transfer.h"
#include "vault/v3_db.h"
#include "vault/v3_fs.h"
#include "vault/v3_header.h"
#include "vault/v3_object_store.h"
#include "vault/v3_read_session.h"
#include "vault/vault.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace {

void put16(std::span<uint8_t> out, size_t off, uint16_t value)
{
    out[off] = static_cast<uint8_t>(value);
    out[off + 1] = static_cast<uint8_t>(value >> 8);
}

void put32(std::span<uint8_t> out, size_t off, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        out[off + i] = static_cast<uint8_t>(value >> (i * 8));
}

std::array<uint8_t, vault::v3::V3_HEADER_SIZE> make_header()
{
    std::array<uint8_t, vault::v3::V3_HEADER_SIZE> raw{};
    std::memcpy(raw.data(), "OSV3DIR\0", 8);
    put16(raw, 8, vault::v3::FORMAT_VERSION);
    put16(raw, 10, vault::v3::V3_HEADER_SIZE);
    raw[16] = 0;
    put32(raw, 17, 1);
    put32(raw, 21, 8);
    put32(raw, 25, 1);
    raw[45] = 0;
    for (size_t i = 0; i < 16; ++i) {
        raw[29 + i] = static_cast<uint8_t>(0x20 + i);
        raw[118 + i] = static_cast<uint8_t>(0x80 + i);
    }
    for (size_t i = 0; i < 24; ++i)
        raw[46 + i] = static_cast<uint8_t>(0x40 + i);
    return raw;
}

struct TempRoot {
    std::filesystem::path path;
    TempRoot()
    {
        std::array<char, 64> pattern{};
        std::snprintf(pattern.data(), pattern.size(), "/tmp/osv-v3-read-XXXXXX");
        path = ::mkdtemp(pattern.data());
        std::filesystem::remove(path);
    }
    ~TempRoot()
    {
        std::filesystem::remove_all(path);
    }
};

constexpr vault::v3::Id ROOT_ID{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
constexpr vault::v3::Id IMAGE_ID{2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
constexpr vault::v3::Id VIDEO_ID{3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};
constexpr std::array<uint8_t, crypto::KEY_SIZE> MASTER{0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                                                       11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                                                       22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
constexpr std::array<uint8_t, 2> PASSWORD{'p', 'w'};

bool write_fixture_header(const std::filesystem::path& path)
{
    auto raw = make_header();
    vault::v3::V3Header header;
    if (vault::v3::parse_v3_header(raw, header) != vault::v3::HeaderStatus::Ok) return false;
    crypto::SecureBuffer<crypto::KEY_SIZE> kek;
    if (!crypto::derive_key(PASSWORD, {}, header.salt, header.kdf, kek)) return false;
    std::vector<uint8_t> sealed;
    const auto ad = vault::v3::master_wrap_ad(header);
    if (!crypto::seal(kek.as_span(), header.nonce, MASTER, sealed, ad)) return false;
    std::copy_n(sealed.begin(), crypto::KEY_SIZE, raw.begin() + 70);
    std::copy_n(sealed.begin() + crypto::KEY_SIZE, crypto::TAG_SIZE, raw.begin() + 102);
    std::ofstream output(path / "vault.header", std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(raw.data()),
                 static_cast<std::streamsize>(raw.size()));
    return output.good();
}

}  // namespace

TEST(v3_header_parse_rejects_reserved_flags_bounds_and_zero_identity)
{
    auto raw = make_header();
    vault::v3::V3Header header;
    REQUIRE(vault::v3::parse_v3_header(raw, header) == vault::v3::HeaderStatus::Ok);

    raw[12] = 1;
    CHECK(vault::v3::parse_v3_header(raw, header) == vault::v3::HeaderStatus::UnsupportedVersion);
    raw = make_header();
    put32(raw, 21, crypto::MAX_KDF_M_COST_KIB + 1);
    CHECK(vault::v3::parse_v3_header(raw, header) == vault::v3::HeaderStatus::Invalid);
    raw = make_header();
    std::fill(raw.begin() + 118, raw.begin() + 134, 0);
    CHECK(vault::v3::parse_v3_header(raw, header) == vault::v3::HeaderStatus::Invalid);
    raw = make_header();
    raw.back() = 1;
    CHECK(vault::v3::parse_v3_header(raw, header) == vault::v3::HeaderStatus::Invalid);
}

TEST(v3_header_unwrap_authenticates_vault_identity_and_credentials)
{
    auto raw = make_header();
    vault::v3::V3Header header;
    REQUIRE(vault::v3::parse_v3_header(raw, header) == vault::v3::HeaderStatus::Ok);

    crypto::SecureBuffer<crypto::KEY_SIZE> kek;
    REQUIRE(crypto::derive_key(PASSWORD, {}, header.salt, header.kdf, kek));
    std::array<uint8_t, crypto::KEY_SIZE> master{};
    for (size_t i = 0; i < master.size(); ++i)
        master[i] = static_cast<uint8_t>(i + 1);
    const auto ad = vault::v3::master_wrap_ad(header);
    std::vector<uint8_t> sealed;
    REQUIRE(crypto::seal(kek.as_span(), header.nonce, master, sealed, ad));
    std::copy_n(sealed.begin(), crypto::KEY_SIZE, raw.begin() + 70);
    std::copy_n(sealed.begin() + crypto::KEY_SIZE, crypto::TAG_SIZE, raw.begin() + 102);
    REQUIRE(vault::v3::parse_v3_header(raw, header) == vault::v3::HeaderStatus::Ok);

    crypto::SecureBuffer<crypto::KEY_SIZE> opened;
    CHECK(vault::v3::unwrap_v3_master_key(header, PASSWORD, {}, opened) ==
          vault::v3::HeaderStatus::Ok);
    CHECK(std::ranges::equal(master, opened.as_span()));
    constexpr std::array<uint8_t, 3> WRONG{'b', 'a', 'd'};
    CHECK(vault::v3::unwrap_v3_master_key(header, WRONG, {}, opened) ==
          vault::v3::HeaderStatus::AuthenticationFailed);
}

TEST(v3_header_create_serializes_and_reopens_with_only_the_right_credentials)
{
    vault::v3::V3Header created;
    crypto::SecureBuffer<crypto::KEY_SIZE> master;
    const crypto::KdfParams params{1, 8, 1};
    REQUIRE(vault::v3::create_v3_header(PASSWORD, {}, params, created, master) ==
            vault::v3::HeaderStatus::Ok);
    const auto raw = vault::v3::serialize_v3_header(created);
    vault::v3::V3Header parsed;
    REQUIRE(vault::v3::parse_v3_header(raw, parsed) == vault::v3::HeaderStatus::Ok);
    crypto::SecureBuffer<crypto::KEY_SIZE> reopened;
    REQUIRE(vault::v3::unwrap_v3_master_key(parsed, PASSWORD, {}, reopened) ==
            vault::v3::HeaderStatus::Ok);
    CHECK(std::ranges::equal(master.as_span(), reopened.as_span()));
    constexpr std::array<uint8_t, 3> WRONG{'b', 'a', 'd'};
    CHECK(vault::v3::unwrap_v3_master_key(parsed, WRONG, {}, reopened) ==
          vault::v3::HeaderStatus::AuthenticationFailed);
}

TEST(v3_vault_facade_lists_searches_reads_and_rejects_object_rewrites)
{
    TempRoot temp;
    auto root = vault::v3::VaultRoot::create(temp.path);
    REQUIRE(root.has_value());
    REQUIRE(write_fixture_header(temp.path));
    REQUIRE(std::filesystem::remove(temp.path / "vault.db"));
    vault::v3::V3Header header;
    auto raw = make_header();
    REQUIRE(vault::v3::parse_v3_header(raw, header) == vault::v3::HeaderStatus::Ok);
    auto db_key = vault::v3::derive_database_key(MASTER, header.vault_id);
    auto created = vault::v3::Database::create(temp.path / "vault.db", db_key.as_span(), ROOT_ID);
    REQUIRE(created.status == vault::v3::DbStatus::Ok);
    REQUIRE(created.database.has_value());
    vault::v3::NodeRecord image;
    image.node_id = IMAGE_ID;
    image.parent_id = ROOT_ID;
    image.type = vault::v3::NodeType::Image;
    image.display_name = "photo.jpg";
    image.favorite = true;
    image.media_format = 0;
    image.width = 12;
    image.height = 8;
    image.original_size = 5;
    REQUIRE(created.database->insert_node(image) == vault::v3::DbStatus::Ok);
    REQUIRE(created.database->add_tag(1, "place:Berlin", "place:berlin") ==
            vault::v3::DbStatus::Ok);
    REQUIRE(created.database->assign_tag(IMAGE_ID, 1) == vault::v3::DbStatus::Ok);
    const std::array<uint8_t, 5> bytes{1, 2, 3, 4, 5};
    vault::v3::ObjectWriteRequest request{header.vault_id, IMAGE_ID,
                                          vault::v3::ObjectRole::OriginalImage, 0, 64};
    const auto written = vault::v3::write_object(*root, MASTER, request, bytes);
    REQUIRE(written.status == vault::v3::ObjectStatus::Ok);
    REQUIRE(created.database->insert_object(
                {written.info.object_id, IMAGE_ID, vault::v3::ObjectRole::OriginalImage,
                 written.info.encrypted_length, written.info.plaintext_length,
                 written.info.frame_plain_limit, written.info.frame_count, 0}) ==
            vault::v3::DbStatus::Ok);
    request.role = vault::v3::ObjectRole::Thumbnail;
    const auto thumb_written = vault::v3::write_object(*root, MASTER, request, bytes);
    REQUIRE(thumb_written.status == vault::v3::ObjectStatus::Ok);
    REQUIRE(created.database->insert_object(
                {thumb_written.info.object_id, IMAGE_ID, vault::v3::ObjectRole::Thumbnail,
                 thumb_written.info.encrypted_length, thumb_written.info.plaintext_length,
                 thumb_written.info.frame_plain_limit, thumb_written.info.frame_count, 0}) ==
            vault::v3::DbStatus::Ok);
    vault::v3::NodeRecord video;
    video.node_id = VIDEO_ID;
    video.parent_id = ROOT_ID;
    video.type = vault::v3::NodeType::Video;
    video.display_name = "clip.mp4";
    video.sibling_order = 1;
    video.media_format = 0;
    video.original_size = 5;
    REQUIRE(created.database->insert_node(video) == vault::v3::DbStatus::Ok);
    const auto video_written = vault::v3::write_video_object_stream(
        *root, MASTER, header.vault_id, VIDEO_ID, 0, bytes.size(),
        [&](uint64_t offset, std::span<uint8_t> destination) {
            std::copy_n(bytes.begin() + static_cast<size_t>(offset), destination.size(),
                        destination.begin());
            return true;
        });
    REQUIRE(video_written.status == vault::v3::ObjectStatus::Ok);
    REQUIRE(created.database->insert_object(
                {video_written.info.object_id, VIDEO_ID, vault::v3::ObjectRole::OriginalVideo,
                 video_written.info.encrypted_length, video_written.info.plaintext_length,
                 video_written.info.frame_plain_limit, video_written.info.frame_count, 0}) ==
            vault::v3::DbStatus::Ok);
    created.database.reset();
    root.reset();

    vault::Vault opened;
    REQUIRE(vault::Vault::open(temp.path.string(), opened) == vault::VaultResult::Ok);
    CHECK_FALSE(vault_is_read_only(opened));
    REQUIRE(opened.unlock(PASSWORD, {}) == vault::VaultResult::Ok);
    const auto listing = opened.list("");
    REQUIRE(listing.size() == 2U);
    CHECK(listing[0]->name == "photo.jpg");
    CHECK_EQ(listing[0]->tags.size(), 1U);
    CHECK_EQ(opened.search("berlin", vault::SearchScope::Both).size(), 1U);
    crypto::SecureBytes read;
    REQUIRE(opened.read_image(*listing[0], read) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(read.as_span(), bytes);
    REQUIRE(opened.read_thumbnail(*listing[0], read) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(read.as_span(), bytes);
    REQUIRE(vault::read_thumb_span(opened, vault::media_thumb_chunk_ref(*listing[0]), read) ==
            vault::VaultResult::Ok);
    auto source = media::VideoSource::open(opened, *listing[1]);
    std::array<uint8_t, 5> streamed{};
    CHECK_EQ(source.read(0, streamed), 5);
    CHECK_BYTES_EQ(streamed, bytes);
    CHECK(opened.create_gallery("metadata-write") == vault::VaultResult::Ok);
    CHECK(vault::uses_context_chunks(opened));
    CHECK(vault::apply_image_animated(opened, "photo.jpg", true) == vault::VaultResult::Ok);
    CHECK(vault::apply_image_thumb(opened, "photo.jpg", bytes) == vault::VaultResult::Ok);
    CHECK(vault::apply_video_poster(opened, "clip.mp4", bytes) == vault::VaultResult::Ok);
    CHECK(vault::apply_video_probe(opened, "clip.mp4", {}) == vault::VaultResult::Ok);
    CHECK(vault::apply_context_rewrite(opened, "photo.jpg") == vault::VaultResult::InvalidArg);
    CHECK(vault::finalize_context_migration(opened) == vault::VaultResult::InvalidArg);
    CHECK(vault::commit_migration(opened, vault::vault_settings(opened)) == vault::VaultResult::Ok);
    vault::Vault competing;
    REQUIRE(vault::Vault::open(temp.path.string(), competing) == vault::VaultResult::Ok);
    CHECK(competing.unlock(PASSWORD, {}) == vault::VaultResult::Busy);
    opened.lock();
    CHECK_FALSE(opened.is_unlocked());
    CHECK(competing.unlock(PASSWORD, {}) == vault::VaultResult::Ok);
}

TEST(v3_vault_facade_creates_directory_without_changing_legacy_create)
{
    TempRoot temp;
    vault::Vault created;
    const crypto::KdfParams params{1, 8, 1};
    REQUIRE(vault::Vault::create_directory(temp.path.string(), PASSWORD, {}, params, created) ==
            vault::VaultResult::Ok);
    CHECK(created.is_unlocked());
    CHECK_FALSE(vault::vault_is_read_only(created));
    CHECK(std::filesystem::is_directory(temp.path));
    REQUIRE(created.create_gallery("album/nested") == vault::VaultResult::Ok);
    const std::array<uint8_t, 5> original{9, 8, 7, 6, 5};
    REQUIRE(created.add_image("album", original, "raw.bin") == vault::VaultResult::Ok);
    REQUIRE(created.create_gallery("destination") == vault::VaultResult::Ok);
    REQUIRE(vault::transfer_image(created, "album", "raw.bin", created, "destination",
                                  vault::TransferMode::Move) == vault::VaultResult::Ok);
    REQUIRE(created.add_tag("album", "place:Berlin") == vault::VaultResult::Ok);
    REQUIRE(vault::toggle_favorite_node(created, "album") == vault::VaultResult::Ok);
    REQUIRE(vault::rename_node(created, "album", "nested", "renamed") == vault::VaultResult::Ok);
    auto settings = vault::vault_settings(created);
    settings.tiles_show_tags = false;
    REQUIRE(vault::set_vault_settings(created, std::move(settings)) == vault::VaultResult::Ok);
    constexpr std::array<uint8_t, 3> NEW_PASSWORD{'n', 'e', 'w'};
    vault::v3::inject_fs_fault(vault::v3::FsFault::Rename);
    CHECK_EQ(static_cast<int>(created.change_password(PASSWORD, {}, NEW_PASSWORD, {})),
             static_cast<int>(vault::VaultResult::IoError));
    vault::v3::clear_fs_faults();
    REQUIRE(created.change_password(PASSWORD, {}, NEW_PASSWORD, {}) == vault::VaultResult::Ok);
    created.lock();

    vault::Vault reopened;
    REQUIRE(vault::Vault::open(temp.path.string(), reopened) == vault::VaultResult::Ok);
    CHECK(reopened.unlock(PASSWORD, {}) == vault::VaultResult::AuthFailed);
    REQUIRE(reopened.unlock(NEW_PASSWORD, {}) == vault::VaultResult::Ok);
    const auto top = reopened.list("");
    REQUIRE(top.size() == 2U);
    CHECK(top[0]->name == "album");
    CHECK(top[0]->favorite);
    REQUIRE(top[0]->tags.size() == 1U);
    CHECK(top[0]->tags[0] == "place:Berlin");
    const auto nested = reopened.list("album");
    REQUIRE(nested.size() == 1U);
    CHECK(nested[0]->name == "renamed");
    const auto destination = reopened.list("destination");
    REQUIRE(destination.size() == 1U);
    CHECK(destination[0]->name == "raw.bin");
    crypto::SecureBytes roundtrip;
    CHECK_EQ(static_cast<int>(reopened.read_image(*destination[0], roundtrip)),
             static_cast<int>(vault::VaultResult::Ok));
    CHECK_BYTES_EQ(roundtrip.as_span(), original);
    CHECK_FALSE(vault::vault_settings(reopened).tiles_show_tags);
}

TEST(opened_legacy_vault_is_read_only_after_directory_cutover)
{
    TempRoot temp;
    const crypto::KdfParams params{1, 8, 1};
    {
        vault::Vault legacy_writer;
        REQUIRE(vault::Vault::create(temp.path.string(), PASSWORD, {}, params, legacy_writer) ==
                vault::VaultResult::Ok);
        REQUIRE(legacy_writer.create_gallery("existing") == vault::VaultResult::Ok);
    }

    vault::Vault opened;
    REQUIRE(vault::Vault::open(temp.path.string(), opened) == vault::VaultResult::Ok);
    REQUIRE(opened.unlock(PASSWORD, {}) == vault::VaultResult::Ok);
    CHECK(vault::vault_is_read_only(opened));
    CHECK(opened.create_gallery("blocked") == vault::VaultResult::InvalidArg);
    CHECK_EQ(opened.list("").size(), 1U);
}

TEST(v3_directory_create_failure_rolls_back_only_its_new_root)
{
    TempRoot temp;
    vault::v3::inject_fs_fault(vault::v3::FsFault::Write);
    vault::Vault output;
    const crypto::KdfParams params{1, 8, 1};
    CHECK(vault::Vault::create_directory(temp.path.string(), PASSWORD, {}, params, output) ==
          vault::VaultResult::IoError);
    vault::v3::clear_fs_faults();
    CHECK_FALSE(std::filesystem::exists(temp.path));
}

TEST(v3_directory_create_refuses_existing_paths_and_locked_password_rotation_works)
{
    TempRoot temp;
    std::filesystem::create_directory(temp.path);
    vault::Vault output;
    const crypto::KdfParams params{1, 8, 1};
    CHECK(vault::Vault::create_directory(temp.path.string(), PASSWORD, {}, params, output) ==
          vault::VaultResult::AlreadyExists);
    std::filesystem::remove(temp.path);
    REQUIRE(vault::Vault::create_directory(temp.path.string(), PASSWORD, {}, params, output) ==
            vault::VaultResult::Ok);
    output.lock();
    constexpr std::array<uint8_t, 7> replacement{'c', 'h', 'a', 'n', 'g', 'e', 'd'};
    REQUIRE(output.change_password(PASSWORD, {}, replacement, {}) == vault::VaultResult::Ok);
    CHECK(output.unlock(PASSWORD, {}) == vault::VaultResult::AuthFailed);
    CHECK(output.unlock(replacement, {}) == vault::VaultResult::Ok);
}

TEST(v3_directory_operational_facade_verifies_maintains_collects_and_backs_up)
{
    TempRoot source;
    TempRoot backup;
    const crypto::KdfParams params{1, 8, 1};
    vault::Vault directory;
    REQUIRE(vault::Vault::create_directory(source.path.string(), PASSWORD, {}, params, directory) ==
            vault::VaultResult::Ok);
    constexpr std::array<uint8_t, 5> bytes{5, 4, 3, 2, 1};
    REQUIRE(directory.add_image("", bytes, "kept.bin") == vault::VaultResult::Ok);

    const auto quick = vault::verify_directory_vault(directory, vault::v3::VerifyDepth::Quick);
    CHECK(quick.status == vault::v3::RecoveryStatus::Ok);
    CHECK(quick.referenced_bytes > 0);
    const auto deep = vault::verify_directory_vault(directory, vault::v3::VerifyDepth::Deep);
    CHECK(deep.status == vault::v3::RecoveryStatus::Ok);
    CHECK_EQ(deep.authenticated_bytes, bytes.size());
    const auto before_bytes = vault::directory_vault_database_bytes(directory);
    REQUIRE(before_bytes.has_value());
    CHECK(*before_bytes > 0);
    CHECK(vault::maintain_directory_database(directory) == vault::VaultResult::Ok);

    const auto collected = vault::garbage_collect_directory_vault(directory, 0);
    CHECK(collected.status == vault::v3::RecoveryStatus::Ok);
    CHECK_EQ(collected.deleted, 0U);
    const auto copied = vault::backup_directory_vault(directory, backup.path);
    REQUIRE(copied.status == vault::v3::BackupStatus::Ok);
    CHECK_EQ(copied.copied_objects, 1U);

    vault::Vault restored;
    REQUIRE(vault::Vault::open(backup.path.string(), restored) == vault::VaultResult::Ok);
    REQUIRE(restored.unlock(PASSWORD, {}) == vault::VaultResult::Ok);
    const auto listing = restored.list("");
    REQUIRE(listing.size() == 1U);
    crypto::SecureBytes roundtrip;
    REQUIRE(restored.read_image(*listing[0], roundtrip) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(roundtrip.as_span(), bytes);

    directory.lock();
    CHECK_FALSE(vault::directory_vault_database_bytes(directory).has_value());
    CHECK(vault::maintain_directory_database(directory) == vault::VaultResult::IoError);
    CHECK(vault::backup_directory_vault(directory, source.path.parent_path() / "locked-backup")
              .status == vault::v3::BackupStatus::InvalidArgument);
    CHECK(vault::garbage_collect_directory_vault(directory, 0).status ==
          vault::v3::RecoveryStatus::FilesystemError);

    vault::Vault legacy;
    TempRoot legacy_path;
    REQUIRE(vault::Vault::create(legacy_path.path.string(), PASSWORD, {}, params, legacy) ==
            vault::VaultResult::Ok);
    CHECK(vault::verify_directory_vault(legacy, vault::v3::VerifyDepth::Quick).status ==
          vault::v3::RecoveryStatus::FilesystemError);
    CHECK_FALSE(vault::directory_vault_database_bytes(legacy).has_value());
    CHECK(vault::maintain_directory_database(legacy) == vault::VaultResult::InvalidArg);
    CHECK(
        vault::backup_directory_vault(legacy, source.path.parent_path() / "legacy-backup").status ==
        vault::v3::BackupStatus::InvalidArgument);
}

TEST(v3_prestaged_image_persists_original_thumbnail_and_metadata)
{
    TempRoot temp;
    vault::Vault created;
    const crypto::KdfParams params{1, 8, 1};
    REQUIRE(vault::Vault::create_directory(temp.path.string(), PASSWORD, {}, params, created) ==
            vault::VaultResult::Ok);
    constexpr std::array<uint8_t, 4> original{4, 3, 2, 1};
    constexpr std::array<uint8_t, 6> jpeg{0xff, 0xd8, 9, 8, 0xff, 0xd9};
    vault::StagedThumb thumb;
    REQUIRE(thumb.thumb_jpeg.assign(jpeg));
    thumb.format = vault::ImageFormat::JPEG;
    thumb.width = 321;
    thumb.height = 123;
    REQUIRE(vault::add_image_prestaged(created, "", original, "pre.jpg", thumb, 424242) ==
            vault::VaultResult::Ok);
    created.lock();
    REQUIRE(created.unlock(PASSWORD, {}) == vault::VaultResult::Ok);
    const auto listing = created.list("");
    REQUIRE(listing.size() == 1U);
    CHECK(listing[0]->meta.format == vault::ImageFormat::JPEG);
    CHECK_EQ(listing[0]->meta.width, 321U);
    CHECK_EQ(listing[0]->meta.height, 123U);
    CHECK_EQ(listing[0]->meta.created_ts, 424242U);
    crypto::SecureBytes original_back;
    crypto::SecureBytes thumb_back;
    REQUIRE(created.read_image(*listing[0], original_back) == vault::VaultResult::Ok);
    REQUIRE(created.read_thumbnail(*listing[0], thumb_back) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(original_back.as_span(), original);
    CHECK_BYTES_EQ(thumb_back.as_span(), jpeg);
}

TEST(v3_unattached_staged_object_remains_readable_until_session_ends)
{
    TempRoot temp;
    const crypto::KdfParams params{1, 8, 1};
    auto created = vault::v3::ReadSession::create(temp.path, PASSWORD, {}, params);
    REQUIRE(created.status == vault::v3::ReadStatus::Ok);
    REQUIRE(created.session != nullptr);
    vault::v3::Id abandoned{};
    abandoned[0] = 0xa5;
    constexpr std::array<uint8_t, 3> bytes{1, 2, 3};
    REQUIRE(created.session->stage_object(abandoned, vault::v3::ObjectRole::OriginalImage,
                                          static_cast<uint8_t>(vault::ImageFormat::Unknown),
                                          bytes) == vault::v3::ReadStatus::Ok);
    REQUIRE(created.session->commit_metadata(created.session->root(), created.session->settings(),
                                             {}) == vault::v3::ReadStatus::Ok);
    crypto::SecureBytes abandoned_plain;
    CHECK(created.session->read_id(abandoned, vault::v3::ObjectRole::OriginalImage,
                                   abandoned_plain) == vault::v3::ReadStatus::Ok);

    auto root = created.session->root();
    auto live = vault::IndexNode::image("live.bin");
    live.node_id[0] = 0x5a;
    live.meta.format = vault::ImageFormat::Unknown;
    live.meta.orig_size = bytes.size();
    REQUIRE(created.session->stage_object(live.node_id, vault::v3::ObjectRole::OriginalImage,
                                          static_cast<uint8_t>(live.meta.format),
                                          bytes) == vault::v3::ReadStatus::Ok);
    root.children.push_back(live);
    REQUIRE(created.session->commit_metadata(root, created.session->settings(), {}) ==
            vault::v3::ReadStatus::Ok);
    crypto::SecureBytes roundtrip;
    REQUIRE(created.session->read(root.children[0], vault::v3::ObjectRole::OriginalImage,
                                  roundtrip) == vault::v3::ReadStatus::Ok);
    CHECK_BYTES_EQ(roundtrip.as_span(), bytes);
    created.session->lock();
    REQUIRE(created.session->unlock(PASSWORD, {}) == vault::v3::ReadStatus::Ok);
    CHECK(created.session->read_id(abandoned, vault::v3::ObjectRole::OriginalImage,
                                   abandoned_plain) == vault::v3::ReadStatus::BadFormat);
}

TEST(v3_and_legacy_apply_the_same_user_visible_mutation_contract)
{
    TempRoot legacy_path;
    TempRoot directory_path;
    const crypto::KdfParams params{1, 8, 1};
    vault::Vault legacy;
    vault::Vault directory;
    REQUIRE(vault::Vault::create(legacy_path.path.string(), PASSWORD, {}, params, legacy) ==
            vault::VaultResult::Ok);
    REQUIRE(vault::Vault::create_directory(directory_path.path.string(), PASSWORD, {}, params,
                                           directory) == vault::VaultResult::Ok);
    constexpr std::array<uint8_t, 5> bytes{7, 1, 4, 2, 9};
    const auto mutate = [&](vault::Vault& value) {
        REQUIRE(value.create_gallery("trips/2026") == vault::VaultResult::Ok);
        REQUIRE(value.add_image("trips/2026", bytes, "photo.bin") == vault::VaultResult::Ok);
        REQUIRE(value.set_tags("trips/2026/photo.bin", {"place:Berlin", "night"}) ==
                vault::VaultResult::Ok);
        REQUIRE(vault::toggle_favorite_node(value, "trips/2026/photo.bin") ==
                vault::VaultResult::Ok);
        REQUIRE(vault::rename_node(value, "trips/2026", "photo.bin", "renamed.bin") ==
                vault::VaultResult::Ok);
        auto settings = vault::vault_settings(value);
        settings.tiles_show_tags = false;
        REQUIRE(vault::set_vault_settings(value, std::move(settings)) == vault::VaultResult::Ok);
    };
    mutate(legacy);
    mutate(directory);
    legacy.lock();
    directory.lock();
    REQUIRE(legacy.unlock(PASSWORD, {}) == vault::VaultResult::Ok);
    REQUIRE(directory.unlock(PASSWORD, {}) == vault::VaultResult::Ok);
    const auto legacy_nodes = legacy.list("trips/2026");
    const auto directory_nodes = directory.list("trips/2026");
    REQUIRE(legacy_nodes.size() == 1U);
    REQUIRE(directory_nodes.size() == 1U);
    CHECK(legacy_nodes[0]->name == directory_nodes[0]->name);
    CHECK(legacy_nodes[0]->favorite == directory_nodes[0]->favorite);
    REQUIRE(legacy_nodes[0]->tags.size() == directory_nodes[0]->tags.size());
    for (const auto& tag : legacy_nodes[0]->tags)
        CHECK(std::ranges::any_of(directory_nodes[0]->tags,
                                  [&](const auto& candidate) { return candidate == tag; }));
    crypto::SecureBytes legacy_plain;
    crypto::SecureBytes directory_plain;
    REQUIRE(legacy.read_image(*legacy_nodes[0], legacy_plain) == vault::VaultResult::Ok);
    REQUIRE(directory.read_image(*directory_nodes[0], directory_plain) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(legacy_plain.as_span(), directory_plain.as_span());
    CHECK(vault::vault_settings(legacy).tiles_show_tags ==
          vault::vault_settings(directory).tiles_show_tags);
}
