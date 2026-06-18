#include "test_framework.h"

#ifdef OSV_VENDORED_AV

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

#include "media/video_decoder.h"
#include "media/chunk_avio.h"
#include "media/video_source.h"
#include "vault/vault.h"
#include "crypto/secure_mem.h"

namespace {

namespace fs = std::filesystem;

// Test KDF params: cheap Argon2 so the test suite stays fast.
static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

// RAII temp path for a unique .osv file.
struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_test_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    std::string str() const { return path.string(); }
};

// Read a file into a vector.
std::vector<uint8_t> read_file(const char* file_path)
{
    std::ifstream f(file_path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// Convert std::string to span of bytes for password.
static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

} // namespace

TEST(video_decoder_decodes_all_frames)
{
    auto v_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");   // h264 yuv420p 160x120 10 frames
    REQUIRE(!v_bytes.empty());
    TempVault tv("decoder_frames");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);

    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(dec.width()  == 160);
    CHECK(dec.height() == 120);
    CHECK(dec.codec()  == vault::VideoCodec::H264);
    CHECK(dec.duration_us() > 0);

    int n = 0;
    double last_pts = -1.0;
    while (auto f = dec.next_frame()) {
        CHECK(f->width == 160);
        CHECK(f->height == 120);
        CHECK(f->pix_fmt == media::FramePixelFormat::I420);
        CHECK(f->pts_seconds >= last_pts);     // non-decreasing
        last_pts = f->pts_seconds;
        ++n;
    }
    CHECK(n == 10);
}

TEST(video_decoder_open_rejects_garbage)
{
    // Store truncated tiny.mp4 (first 200 bytes) — not a valid video header.
    // When we try to open it via ChunkAvio → VideoDecoder, open() should return false.
    auto v_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!v_bytes.empty());

    // Truncate to first 200 bytes: not enough to form a valid video header.
    std::vector<uint8_t> truncated(v_bytes.begin(), v_bytes.begin() + 200);

    TempVault tv("decoder_garbage");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", truncated, "garbage.mp4", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);

    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    // open() must return false on garbage input without crashing
    CHECK(dec.open(avio.ctx()) == false);
}

TEST(video_decoder_seek_lands_on_timestamp)
{
    auto v_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!v_bytes.empty());

    TempVault tv("decoder_seek");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);

    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));

    // Seek to 0.5 seconds
    REQUIRE(dec.seek(0.5));
    auto f = dec.next_frame();
    REQUIRE(f.has_value());
    // Frame should land on or after 0.5s, but within ~2 frames (0.2s at 10fps)
    CHECK(f->pts_seconds >= 0.5);
    CHECK(f->pts_seconds < 0.7);

    // Seek back to the beginning
    REQUIRE(dec.seek(0.0));
    auto f0 = dec.next_frame();
    REQUIRE(f0.has_value());
    CHECK(f0->pts_seconds < 0.15);
}

TEST(video_decoder_swscale_converts_yuv444)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny444.mp4");
    REQUIRE(!v_bytes.empty());

    TempVault tv("decoder_swscale");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "tiny444.mp4", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);

    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(dec.width() == 160);
    CHECK(dec.height() == 120);

    int frame_count = 0;
    while (auto f = dec.next_frame()) {
        // Should be converted to I420 by swscale fallback
        CHECK(f->pix_fmt == media::FramePixelFormat::I420);
        CHECK(f->width == 160);
        CHECK(f->height == 120);
        CHECK(f->planes[0] != nullptr);
        CHECK(f->planes[1] != nullptr);
        CHECK(f->planes[2] != nullptr);
        ++frame_count;
    }
    CHECK(frame_count > 0);
}

TEST(video_decoder_handles_truncated_without_crash)
{
    auto v_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/truncated.mp4");
    REQUIRE(!v_bytes.empty());

    TempVault tv("decoder_truncated");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "truncated.mp4", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);

    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    // open() may succeed if the header is valid
    if (!dec.open(avio.ctx())) {
        // If open fails, that's okay — the stream was too truncated
        return;
    }

    // Drain next_frame() to exhaustion — should terminate gracefully, no infinite loop
    int frame_count = 0;
    while (auto f = dec.next_frame()) {
        ++frame_count;
    }
    // Should have decoded some frames or zero, but not crashed/looped infinitely
    CHECK(frame_count >= 0);
}

#endif // OSV_VENDORED_AV
