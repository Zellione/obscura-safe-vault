#include "test_framework.h"

#include "crypto/aead.h"
#include "crypto/kdf.h"
#include "crypto/random.h"
#include "vault/v3_header.h"
#include "vault/v3_db.h"
#include "vault/v3_fs.h"
#include "vault/v3_object_store.h"
#include "vault/vault.h"
#include "media/video_source.h"

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
    for (unsigned i = 0; i < 4; ++i) out[off + i] = static_cast<uint8_t>(value >> (i * 8));
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
    for (size_t i = 0; i < 24; ++i) raw[46 + i] = static_cast<uint8_t>(0x40 + i);
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
    ~TempRoot() { std::filesystem::remove_all(path); }
};

constexpr vault::v3::Id ROOT_ID{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
constexpr vault::v3::Id IMAGE_ID{2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2};
constexpr vault::v3::Id VIDEO_ID{3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3};
constexpr std::array<uint8_t, crypto::KEY_SIZE> MASTER{
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
    16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
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
    output.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    return output.good();
}

} // namespace

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
    for (size_t i = 0; i < master.size(); ++i) master[i] = static_cast<uint8_t>(i + 1);
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

TEST(v3_vault_facade_lists_searches_reads_and_rejects_mutation)
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
    const std::array<uint8_t, 5> bytes{1,2,3,4,5};
    vault::v3::ObjectWriteRequest request{header.vault_id, IMAGE_ID,
        vault::v3::ObjectRole::OriginalImage, 0, 64};
    const auto written = vault::v3::write_object(*root, MASTER, request, bytes);
    REQUIRE(written.status == vault::v3::ObjectStatus::Ok);
    REQUIRE(created.database->insert_object({written.info.object_id, IMAGE_ID,
        vault::v3::ObjectRole::OriginalImage, written.info.encrypted_length,
        written.info.plaintext_length, written.info.frame_plain_limit,
        written.info.frame_count, 0}) == vault::v3::DbStatus::Ok);
    request.role = vault::v3::ObjectRole::Thumbnail;
    const auto thumb_written = vault::v3::write_object(*root, MASTER, request, bytes);
    REQUIRE(thumb_written.status == vault::v3::ObjectStatus::Ok);
    REQUIRE(created.database->insert_object({thumb_written.info.object_id, IMAGE_ID,
        vault::v3::ObjectRole::Thumbnail, thumb_written.info.encrypted_length,
        thumb_written.info.plaintext_length, thumb_written.info.frame_plain_limit,
        thumb_written.info.frame_count, 0}) == vault::v3::DbStatus::Ok);
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
    REQUIRE(created.database->insert_object({video_written.info.object_id, VIDEO_ID,
        vault::v3::ObjectRole::OriginalVideo, video_written.info.encrypted_length,
        video_written.info.plaintext_length, video_written.info.frame_plain_limit,
        video_written.info.frame_count, 0}) == vault::v3::DbStatus::Ok);
    created.database.reset();
    root.reset();

    vault::Vault opened;
    REQUIRE(vault::Vault::open(temp.path.string(), opened) == vault::VaultResult::Ok);
    CHECK(vault_is_read_only(opened));
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
    CHECK(opened.create_gallery("must-not-write") == vault::VaultResult::InvalidArg);
    CHECK(vault::uses_context_chunks(opened));
    CHECK(vault::apply_image_animated(opened, "photo.jpg", true) ==
          vault::VaultResult::InvalidArg);
    CHECK(vault::apply_image_thumb(opened, "photo.jpg", bytes) ==
          vault::VaultResult::InvalidArg);
    CHECK(vault::apply_video_poster(opened, "clip.mp4", bytes) ==
          vault::VaultResult::InvalidArg);
    CHECK(vault::apply_video_probe(opened, "clip.mp4", {}) ==
          vault::VaultResult::InvalidArg);
    CHECK(vault::apply_context_rewrite(opened, "photo.jpg") ==
          vault::VaultResult::InvalidArg);
    CHECK(vault::finalize_context_migration(opened) == vault::VaultResult::InvalidArg);
    CHECK(vault::commit_migration(opened, vault::vault_settings(opened)) ==
          vault::VaultResult::InvalidArg);
    vault::Vault competing;
    REQUIRE(vault::Vault::open(temp.path.string(), competing) == vault::VaultResult::Ok);
    CHECK(competing.unlock(PASSWORD, {}) == vault::VaultResult::Busy);
    opened.lock();
    CHECK_FALSE(opened.is_unlocked());
    CHECK(competing.unlock(PASSWORD, {}) == vault::VaultResult::Ok);
}
