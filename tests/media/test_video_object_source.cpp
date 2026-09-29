#include "test_framework.h"

#include "media/video_object_source.h"
#ifdef OSV_VENDORED_AV
#include "media/chunk_avio.h"
#include "media/video_decoder.h"
#endif
#include "vault/v3_object_store.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

namespace {

struct TempRoot {
    std::filesystem::path path;
    TempRoot()
    {
        std::array<char, 64> pattern{};
        std::snprintf(pattern.data(), pattern.size(), "/tmp/osv-video-object-XXXXXX");
        path = ::mkdtemp(pattern.data());
        std::filesystem::remove(path);
    }
    ~TempRoot() { std::filesystem::remove_all(path); }
};

constexpr vault::v3::Id VAULT_ID{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
constexpr vault::v3::Id NODE_ID{16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
constexpr std::array<uint8_t, crypto::KEY_SIZE> MASTER_KEY{};

uint8_t byte_at(uint64_t offset) { return static_cast<uint8_t>(offset % 251); }

}  // namespace

TEST(video_object_source_boundary_eof_backward_seek_and_cache)
{
    TempRoot temp;
    auto root = vault::v3::VaultRoot::create(temp.path);
    REQUIRE(root.has_value());
    auto lock = root->try_writer_lock();
    REQUIRE(lock.has_value());
    constexpr uint64_t total = 2U * 64U + 7U;
    vault::v3::ObjectWriteRequest request{.vault_id = VAULT_ID,
                                          .owner_node_id = NODE_ID,
                                          .role = vault::v3::ObjectRole::OriginalVideo,
                                          .media_format = 3,
                                          .frame_plain_limit = 64};
    const auto written = vault::v3::write_object_stream(
        *root, MASTER_KEY, request, total, [](uint64_t offset, std::span<uint8_t> out) {
            for (size_t i = 0; i < out.size(); ++i) out[i] = byte_at(offset + i);
            return true;
        });
    REQUIRE(written.status == vault::v3::ObjectStatus::Ok);
    auto opened = media::VideoObjectSource::open(*root, MASTER_KEY, written.info);
    REQUIRE(opened.status == vault::v3::ObjectStatus::Ok);
    REQUIRE(opened.source != nullptr);
    CHECK_EQ(opened.source->size(), total);

    std::array<uint8_t, 90> across{};
    REQUIRE(opened.source->read(50, across) == 85);
    for (size_t i = 0; i < 85; ++i) CHECK_EQ(across[i], byte_at(50 + i));
    std::array<uint8_t, 9> backwards{};
    REQUIRE(opened.source->read(3, backwards) == 9);
    for (size_t i = 0; i < backwards.size(); ++i) CHECK_EQ(backwards[i], byte_at(3 + i));
    CHECK(opened.source->read(total, backwards) == 0);
    CHECK_TRUE(opened.source->cached_frame_count() <= 2);
}

TEST(video_object_source_cancel_is_thread_safe_and_releases_no_bytes_after_cancel)
{
    TempRoot temp;
    auto root = vault::v3::VaultRoot::create(temp.path);
    REQUIRE(root.has_value());
    auto lock = root->try_writer_lock();
    REQUIRE(lock.has_value());
    vault::v3::ObjectWriteRequest request{.vault_id = VAULT_ID,
                                          .owner_node_id = NODE_ID,
                                          .role = vault::v3::ObjectRole::OriginalVideo,
                                          .frame_plain_limit = 64};
    const auto written = vault::v3::write_object_stream(
        *root, MASTER_KEY, request, 256, [](uint64_t offset, std::span<uint8_t> out) {
            for (size_t i = 0; i < out.size(); ++i) out[i] = byte_at(offset + i);
            return true;
        });
    REQUIRE(written.status == vault::v3::ObjectStatus::Ok);
    auto opened = media::VideoObjectSource::open(*root, MASTER_KEY, written.info);
    REQUIRE(opened.source != nullptr);
    std::atomic<bool> reading{false};
    std::thread reader([&] {
        std::array<uint8_t, 127> scratch{};
        reading.store(true, std::memory_order_release);
        while (!opened.source->cancelled())
            (void)opened.source->read(1, scratch);
    });
    while (!reading.load(std::memory_order_acquire)) std::this_thread::yield();
    opened.source->cancel();
    reader.join();
    std::array<uint8_t, 16> out{};
    CHECK(opened.source->read(0, out) == -1);
    CHECK(opened.source->cancelled());
    CHECK_EQ(opened.source->cached_frame_count(), 0U);
}

#ifdef OSV_VENDORED_AV
TEST(video_object_source_drives_ffmpeg_decode_and_seek_through_format_neutral_avio)
{
    std::ifstream file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4", std::ios::binary);
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    REQUIRE(!bytes.empty());
    TempRoot temp;
    auto root = vault::v3::VaultRoot::create(temp.path);
    REQUIRE(root.has_value());
    auto lock = root->try_writer_lock();
    REQUIRE(lock.has_value());
    vault::v3::ObjectWriteRequest request{.vault_id = VAULT_ID,
                                          .owner_node_id = NODE_ID,
                                          .role = vault::v3::ObjectRole::OriginalVideo,
                                          .media_format = 1,
                                          .frame_plain_limit = 4096};
    const auto written = vault::v3::write_object(*root, MASTER_KEY, request, bytes);
    REQUIRE(written.status == vault::v3::ObjectStatus::Ok);
    auto opened = media::VideoObjectSource::open(*root, MASTER_KEY, written.info);
    REQUIRE(opened.source != nullptr);
    media::ChunkAvio avio(std::move(opened.source));
    REQUIRE(avio.valid());
    media::VideoDecoder decoder;
    REQUIRE(decoder.open(avio.ctx()));
    CHECK_EQ(decoder.width(), 160);
    CHECK_EQ(decoder.height(), 120);
    CHECK(decoder.codec() == vault::VideoCodec::H264);
    REQUIRE(decoder.seek(0.4));
    const auto frame = decoder.next_frame();
    REQUIRE(frame.has_value());
    CHECK_EQ(frame->width, 160);
    CHECK_EQ(frame->height, 120);
}
#endif
