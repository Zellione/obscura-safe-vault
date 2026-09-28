#include "test_framework.h"

#include "vault/v3_object_store.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

struct TempRoot {
    std::filesystem::path path;
    TempRoot()
    {
        std::array<char, 64> pattern{};
        std::snprintf(pattern.data(), pattern.size(), "/tmp/osv-v3-object-XXXXXX");
        char* made = ::mkdtemp(pattern.data());
        path = made;
        std::filesystem::remove(path);
    }
    ~TempRoot() { std::filesystem::remove_all(path); }
};

constexpr vault::v3::Id VAULT_ID{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
constexpr vault::v3::Id NODE_ID{16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
constexpr std::array<uint8_t, crypto::KEY_SIZE> MASTER_KEY{
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};

vault::v3::ObjectWriteRequest request()
{
    return {.vault_id = VAULT_ID, .owner_node_id = NODE_ID,
            .role = vault::v3::ObjectRole::OriginalImage, .media_format = 2,
            .frame_plain_limit = 64};
}

}  // namespace

TEST(v3_object_preamble_exact_layout_and_id_generation)
{
    const auto id = vault::v3::generate_object_id();
    REQUIRE(id.has_value());
    CHECK(vault::v3::valid_object_id(*id));
    const auto bytes = vault::v3::build_object_preamble(VAULT_ID, *id, 65536);
    const std::array<uint8_t, 8> magic{'O', 'S', 'V', 'O', 'B', 'J', 0, 0};
    CHECK_BYTES_EQ(std::span(bytes).first<8>(), magic);
    CHECK_EQ(bytes[8], 1);
    CHECK_EQ(bytes[10], 64);
    CHECK_BYTES_EQ((std::span(bytes).subspan<16, 16>()), VAULT_ID);
    CHECK_BYTES_EQ((std::span(bytes).subspan<32, 16>()), *id);
    CHECK_FALSE(vault::v3::valid_object_id({}));
}

TEST(v3_object_round_trip_multiframe_and_range)
{
    TempRoot temp;
    auto root = vault::v3::VaultRoot::create(temp.path);
    REQUIRE(root.has_value());
    auto lock = root->try_writer_lock();
    REQUIRE(lock.has_value());
    crypto::SecureBytes plain;
    REQUIRE(plain.resize(257));
    for (size_t i = 0; i < plain.size(); ++i) plain[i] = static_cast<uint8_t>(i);

    const auto written = vault::v3::write_object(*root, MASTER_KEY, request(), plain.as_span());
    REQUIRE(written.status == vault::v3::ObjectStatus::Ok);
    CHECK_EQ(written.info.frame_count, 5U);

    auto reader = vault::v3::ObjectReader::open(*root, MASTER_KEY, written.info);
    REQUIRE(reader.status == vault::v3::ObjectStatus::Ok);
    crypto::SecureBytes all;
    REQUIRE(reader.reader->read_all(all) == vault::v3::ObjectStatus::Ok);
    CHECK_BYTES_EQ(all.as_span(), plain.as_span());
    crypto::SecureBytes range;
    REQUIRE(reader.reader->read_range(61, 132, range) == vault::v3::ObjectStatus::Ok);
    CHECK_BYTES_EQ(range.as_span(), plain.as_span().subspan(61, 132));
}

TEST(v3_object_substitution_tamper_and_wrong_key_fail_closed)
{
    TempRoot temp;
    auto root = vault::v3::VaultRoot::create(temp.path);
    REQUIRE(root.has_value());
    auto lock = root->try_writer_lock();
    REQUIRE(lock.has_value());
    const std::array<uint8_t, 80> plain{0x5a};
    const auto written = vault::v3::write_object(*root, MASTER_KEY, request(), plain);
    REQUIRE(written.status == vault::v3::ObjectStatus::Ok);

    auto altered = written.info;
    altered.role = vault::v3::ObjectRole::Thumbnail;
    auto wrong_role = vault::v3::ObjectReader::open(*root, MASTER_KEY, altered);
    CHECK(wrong_role.status == vault::v3::ObjectStatus::AuthenticationFailed);
    auto wrong_owner = NODE_ID;
    wrong_owner[0] ^= 1;
    altered = written.info;
    altered.owner_node_id = wrong_owner;
    auto owner_result = vault::v3::ObjectReader::open(*root, MASTER_KEY, altered);
    CHECK(owner_result.status == vault::v3::ObjectStatus::AuthenticationFailed);
    auto wrong_key = MASTER_KEY;
    wrong_key[0] ^= 1;
    auto key_result = vault::v3::ObjectReader::open(*root, wrong_key, written.info);
    CHECK(key_result.status == vault::v3::ObjectStatus::AuthenticationFailed);
}

TEST(v3_object_compresses_repetitive_frames_and_published_file_has_no_plaintext)
{
    TempRoot temp;
    auto root = vault::v3::VaultRoot::create(temp.path);
    REQUIRE(root.has_value());
    auto lock = root->try_writer_lock();
    REQUIRE(lock.has_value());
    std::array<uint8_t, 4096> plain{};
    std::ranges::fill(plain, 0x7b);
    auto req = request();
    req.role = vault::v3::ObjectRole::Thumbnail;
    req.frame_plain_limit = 4096;
    const auto written = vault::v3::write_object(*root, MASTER_KEY, req, plain);
    REQUIRE(written.status == vault::v3::ObjectStatus::Ok);
    CHECK_TRUE(written.info.encrypted_length < plain.size());
    std::ifstream in(temp.path / vault::v3::object_relative_path(written.info.object_id),
                     std::ios::binary);
    const std::vector<uint8_t> disk{std::istreambuf_iterator<char>(in), {}};
    CHECK_TRUE(std::search(disk.begin(), disk.end(), plain.begin(), plain.begin() + 64) == disk.end());
    auto opened = vault::v3::ObjectReader::open(*root, MASTER_KEY, written.info);
    REQUIRE(opened.status == vault::v3::ObjectStatus::Ok);
    crypto::SecureBytes roundtrip;
    REQUIRE(opened.reader->read_all(roundtrip) == vault::v3::ObjectStatus::Ok);
    CHECK_BYTES_EQ(roundtrip.as_span(), plain);
}

TEST(v3_object_data_bitflip_is_detected_before_plaintext_release)
{
    TempRoot temp;
    auto root = vault::v3::VaultRoot::create(temp.path);
    REQUIRE(root.has_value());
    auto lock = root->try_writer_lock();
    REQUIRE(lock.has_value());
    std::array<uint8_t, 100> plain{};
    const auto written = vault::v3::write_object(*root, MASTER_KEY, request(), plain);
    REQUIRE(written.status == vault::v3::ObjectStatus::Ok);
    const auto path = temp.path / vault::v3::object_relative_path(written.info.object_id);
    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(file.good());
    constexpr std::streamoff first_data_cipher = 64 + 104 + 24;
    file.seekg(first_data_cipher);
    char byte = 0;
    file.read(&byte, 1);
    byte ^= 1;
    file.seekp(first_data_cipher);
    file.write(&byte, 1);
    file.close();
    auto opened = vault::v3::ObjectReader::open(*root, MASTER_KEY, written.info);
    REQUIRE(opened.status == vault::v3::ObjectStatus::Ok);
    crypto::SecureBytes output;
    CHECK(opened.reader->read_all(output) == vault::v3::ObjectStatus::AuthenticationFailed);
    CHECK_TRUE(output.empty());
}

TEST(v3_object_write_faults_never_report_a_publishable_object)
{
    for (const auto fault : {vault::v3::FsFault::Write, vault::v3::FsFault::FileSync,
                             vault::v3::FsFault::Rename, vault::v3::FsFault::DirectorySync}) {
        TempRoot temp;
        auto root = vault::v3::VaultRoot::create(temp.path);
        REQUIRE(root.has_value());
        auto lock = root->try_writer_lock();
        REQUIRE(lock.has_value());
        vault::v3::inject_fs_fault(fault);
        const std::array<uint8_t, 96> plain{0x42};
        const auto result = vault::v3::write_object(*root, MASTER_KEY, request(), plain);
        CHECK(result.status != vault::v3::ObjectStatus::Ok);
        vault::v3::clear_fs_faults();
    }
}

TEST(v3_object_streaming_writer_pulls_only_bounded_secure_frames)
{
    TempRoot temp;
    auto root = vault::v3::VaultRoot::create(temp.path);
    REQUIRE(root.has_value());
    auto lock = root->try_writer_lock();
    REQUIRE(lock.has_value());
    auto req = request();
    req.frame_plain_limit = 73;
    constexpr uint64_t total = 1000;
    size_t calls = 0;
    size_t largest = 0;
    const auto result = vault::v3::write_object_stream(*root, MASTER_KEY, req, total,
        [&](uint64_t offset, std::span<uint8_t> destination) {
            ++calls;
            largest = std::max(largest, destination.size());
            for (size_t i = 0; i < destination.size(); ++i)
                destination[i] = static_cast<uint8_t>((offset + i) % 251);
            return true;
        });
    REQUIRE(result.status == vault::v3::ObjectStatus::Ok);
    CHECK_EQ(calls, 14U);
    CHECK_EQ(largest, 73U);
    auto opened = vault::v3::ObjectReader::open(*root, MASTER_KEY, result.info);
    REQUIRE(opened.status == vault::v3::ObjectStatus::Ok);
    crypto::SecureBytes bytes;
    REQUIRE(opened.reader->read_all(bytes) == vault::v3::ObjectStatus::Ok);
    for (size_t i = 0; i < bytes.size(); ++i)
        CHECK_EQ(bytes[i], static_cast<uint8_t>(i % 251));
}

TEST(v3_object_streaming_cancel_removes_unpublished_staging_file)
{
    TempRoot temp;
    auto root = vault::v3::VaultRoot::create(temp.path);
    REQUIRE(root.has_value());
    auto lock = root->try_writer_lock();
    REQUIRE(lock.has_value());
    auto req = request();
    req.frame_plain_limit = 64;
    const auto result = vault::v3::write_object_stream(*root, MASTER_KEY, req, 200,
        [](uint64_t offset, std::span<uint8_t> destination) {
            std::ranges::fill(destination, 0x33);
            return offset == 0;
        });
    CHECK(result.status == vault::v3::ObjectStatus::IoError);
    CHECK_TRUE(std::filesystem::is_empty(temp.path / "staging"));
    CHECK_TRUE(std::filesystem::is_empty(temp.path / "objects"));
}

TEST(v3_object_append_truncate_and_filename_id_mismatch_are_rejected)
{
    TempRoot temp;
    auto root = vault::v3::VaultRoot::create(temp.path);
    REQUIRE(root.has_value());
    auto lock = root->try_writer_lock();
    REQUIRE(lock.has_value());
    const std::array<uint8_t, 90> plain{0xa6};
    const auto first = vault::v3::write_object(*root, MASTER_KEY, request(), plain);
    REQUIRE(first.status == vault::v3::ObjectStatus::Ok);
    const auto first_path = temp.path / vault::v3::object_relative_path(first.info.object_id);
    {
        std::ofstream append(first_path, std::ios::binary | std::ios::app);
        append.put('\0');
    }
    CHECK(vault::v3::ObjectReader::open(*root, MASTER_KEY, first.info).status ==
          vault::v3::ObjectStatus::Corrupt);

    const auto second = vault::v3::write_object(*root, MASTER_KEY, request(), plain);
    REQUIRE(second.status == vault::v3::ObjectStatus::Ok);
    const auto second_path = temp.path / vault::v3::object_relative_path(second.info.object_id);
    REQUIRE(::truncate(second_path.c_str(), static_cast<off_t>(second.info.encrypted_length - 1)) == 0);
    CHECK(vault::v3::ObjectReader::open(*root, MASTER_KEY, second.info).status ==
          vault::v3::ObjectStatus::Corrupt);

    const auto third = vault::v3::write_object(*root, MASTER_KEY, request(), plain);
    REQUIRE(third.status == vault::v3::ObjectStatus::Ok);
    auto false_id = third.info.object_id;
    false_id[0] ^= 0x80;
    const auto false_path = temp.path / vault::v3::object_relative_path(false_id);
    std::filesystem::create_directories(false_path.parent_path());
    std::filesystem::rename(temp.path / vault::v3::object_relative_path(third.info.object_id),
                            false_path);
    auto false_info = third.info;
    false_info.object_id = false_id;
    CHECK(vault::v3::ObjectReader::open(*root, MASTER_KEY, false_info).status ==
          vault::v3::ObjectStatus::Corrupt);
}

TEST(v3_object_equal_length_frame_swap_authentication_fails)
{
    TempRoot temp;
    auto root = vault::v3::VaultRoot::create(temp.path);
    REQUIRE(root.has_value());
    auto lock = root->try_writer_lock();
    REQUIRE(lock.has_value());
    auto req = request();
    req.frame_plain_limit = 64;
    std::array<uint8_t, 128> plain{};
    for (size_t i = 0; i < plain.size(); ++i) plain[i] = static_cast<uint8_t>(i);
    const auto written = vault::v3::write_object(*root, MASTER_KEY, req, plain);
    REQUIRE(written.status == vault::v3::ObjectStatus::Ok);
    const auto path = temp.path / vault::v3::object_relative_path(written.info.object_id);
    constexpr size_t frame_record = 64 + 1 + crypto::NONCE_SIZE + crypto::TAG_SIZE;
    constexpr std::streamoff first_offset = 64 + 104;
    std::array<char, frame_record> a{}, b{};
    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(file.good());
    file.seekg(first_offset);
    file.read(a.data(), a.size());
    file.read(b.data(), b.size());
    REQUIRE(file.good());
    file.seekp(first_offset);
    file.write(b.data(), b.size());
    file.write(a.data(), a.size());
    file.close();
    auto opened = vault::v3::ObjectReader::open(*root, MASTER_KEY, written.info);
    REQUIRE(opened.status == vault::v3::ObjectStatus::Ok);
    crypto::SecureBytes out;
    CHECK(opened.reader->read_all(out) == vault::v3::ObjectStatus::AuthenticationFailed);
    CHECK_TRUE(out.empty());
}

TEST(v3_object_inspection_reports_only_authenticated_structural_metadata)
{
    TempRoot temp;
    auto root = vault::v3::VaultRoot::create(temp.path);
    REQUIRE(root.has_value());
    auto lock = root->try_writer_lock();
    REQUIRE(lock.has_value());
    const std::array<uint8_t, 33> plain{0x19};
    const auto written = vault::v3::write_object(*root, MASTER_KEY, request(), plain);
    REQUIRE(written.status == vault::v3::ObjectStatus::Ok);
    const auto inspection = vault::v3::inspect_object(*root, MASTER_KEY, written.info);
    CHECK(inspection.status == vault::v3::ObjectStatus::Ok);
    CHECK_EQ(inspection.info.object_id, written.info.object_id);
    CHECK_EQ(inspection.info.plaintext_length, 33U);
    auto hostile = written.info;
    hostile.frame_count = vault::v3::OBJECT_MAX_FRAMES + 1;
    CHECK(vault::v3::inspect_object(*root, MASTER_KEY, hostile).status ==
          vault::v3::ObjectStatus::InvalidArgument);
}

TEST(v3_object_distinct_image_thumbnail_and_poster_roles_round_trip)
{
    TempRoot temp;
    auto root = vault::v3::VaultRoot::create(temp.path);
    REQUIRE(root.has_value());
    auto lock = root->try_writer_lock();
    REQUIRE(lock.has_value());
    const std::array<uint8_t, 81> plain{0xd4};
    for (const auto role : {vault::v3::ObjectRole::OriginalImage,
                            vault::v3::ObjectRole::Thumbnail,
                            vault::v3::ObjectRole::Poster}) {
        auto req = request();
        req.role = role;
        const auto written = vault::v3::write_object(*root, MASTER_KEY, req, plain);
        REQUIRE(written.status == vault::v3::ObjectStatus::Ok);
        auto opened = vault::v3::ObjectReader::open(*root, MASTER_KEY, written.info);
        REQUIRE(opened.status == vault::v3::ObjectStatus::Ok);
        crypto::SecureBytes out;
        REQUIRE(opened.reader->read_all(out) == vault::v3::ObjectStatus::Ok);
        CHECK_BYTES_EQ(out.as_span(), plain);
    }
}
