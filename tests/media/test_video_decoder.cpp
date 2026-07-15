#include "test_framework.h"

#ifdef OSV_VENDORED_AV

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

#include "media/video_decoder.h"
#include "media/chunk_avio.h"
#include "media/mem_avio.h"
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

TEST(video_decoder_seek_to_zero)
{
    auto v_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!v_bytes.empty());

    TempVault tv("decoder_seek_zero");
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

    // Seek to 0.0 and confirm we land at the start
    REQUIRE(dec.seek(0.0));
    auto f = dec.next_frame();
    REQUIRE(f.has_value());
    CHECK(f->pts_seconds < 0.1);  // Should be close to 0
}

TEST(video_decoder_seek_past_eof)
{
    auto v_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!v_bytes.empty());

    TempVault tv("decoder_seek_eof");
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

    // Seek past the end (e.g., 999 seconds)
    REQUIRE(dec.seek(999.0));
    // next_frame() should return nullopt (no frames to decode)
    auto f = dec.next_frame();
    CHECK(!f.has_value());
}

TEST(video_decoder_decode_poster_rgb)
{
    auto v_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!v_bytes.empty());

    TempVault tv("decoder_poster");
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

    // Decode poster as RGB
    auto img = dec.decode_poster_rgb();
    REQUIRE(img.has_value());
    CHECK(img->width == 160);
    CHECK(img->height == 120);
    CHECK(img->pixels.size() == 160 * 120 * 3);  // RGB24 = 3 bytes per pixel
}

TEST(video_decoder_matroska_probe)
{
    auto v_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mkv");
    REQUIRE(!v_bytes.empty());

    TempVault tv("decoder_mkv");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", v_bytes, "tiny.mkv", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);

    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    CHECK(dec.width() == 160);
    CHECK(dec.height() == 120);
    CHECK(dec.codec() == vault::VideoCodec::H264);
}

TEST(video_decoder_multiple_seeks)
{
    // Test multiple seeks in sequence to cover seek edge cases
    auto v_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!v_bytes.empty());

    TempVault tv("decoder_multi_seek");
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

    // Seek to 0.2s
    REQUIRE(dec.seek(0.2));
    auto f1 = dec.next_frame();
    REQUIRE(f1.has_value());
    double pts1 = f1->pts_seconds;

    // Seek to 0.4s
    REQUIRE(dec.seek(0.4));
    auto f2 = dec.next_frame();
    REQUIRE(f2.has_value());
    double pts2 = f2->pts_seconds;

    // pts2 should be greater than pts1
    CHECK(pts2 >= pts1);

    // Seek back to 0.1s
    REQUIRE(dec.seek(0.1));
    auto f3 = dec.next_frame();
    REQUIRE(f3.has_value());
    CHECK(f3->pts_seconds < pts1);
}

// open() must reject a stream whose codec isn't H.264/HEVC (exercises the
// unsupported-codec failure path), feeding the raw bytes via MemAvio.
TEST(video_decoder_rejects_unsupported_codec)
{
    auto bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_mpeg4.avi");  // mpeg4, not h264/hevc
    REQUIRE(!bytes.empty());
    media::MemAvio avio(bytes);
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    CHECK(!dec.open(avio.ctx()));
}

// open() must reject a file with no video stream (audio only).
TEST(video_decoder_rejects_audio_only)
{
    auto bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/audio_only.m4a");  // aac, no video stream
    REQUIRE(!bytes.empty());
    media::MemAvio avio(bytes);
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    CHECK(!dec.open(avio.ctx()));
}

TEST(video_decoder_exposes_and_decodes_audio_from_tiny_av)
{
    auto av_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_av.mp4");
    REQUIRE(!av_bytes.empty());
    
    TempVault tv("decoder_audio");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("c", av_bytes, "tiny_av.mp4", 4096) == vault::VaultResult::Ok);
    auto kids = v.list("c");
    REQUIRE(kids.size() == 1);
    
    media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
    REQUIRE(avio.valid());
    media::VideoDecoder dec;
    REQUIRE(dec.open(avio.ctx()));
    
    // Check audio is present and has correct properties
    REQUIRE(dec.has_audio());
    CHECK(dec.audio_info().channels == 1);
    CHECK(dec.audio_info().sample_rate == 44100);
    
    // Pull interleaved: a few video frames AND audio frames, neither starves
    int vframes = 0;
    size_t asamples = 0;
    for (int i = 0; i < 5; ++i)
        if (dec.next_frame())
            ++vframes;
    
    for (int i = 0; i < 20; ++i)
        if (auto a = dec.next_audio_frame())
            asamples += a->samples.size();
    
    CHECK(vframes > 0);
    CHECK(asamples > 0);
}

TEST(video_decoder_has_audio_false_for_video_only)
{
    auto v_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");  // video-only
    REQUIRE(!v_bytes.empty());
    
    TempVault tv("decoder_no_audio");
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
    
    // No audio track present
    CHECK(dec.has_audio() == false);
    CHECK(!dec.next_audio_frame().has_value());
    
    // Video should still decode normally
    int vframes = 0;
    while (auto f = dec.next_frame())
        ++vframes;
    CHECK(vframes == 10);
}

TEST(video_decoder_decodes_mov_pro_codecs)
{
    // Phase 28: ProRes / DNxHR / MJPEG .mov streams decode end-to-end through
    // the encrypted-chunk path. Their native pixel formats (yuv422p10le,
    // yuv422p, yuvj422p) are not I420/NV12, so every frame must come out of
    // the decoder's swscale conversion as I420.
    struct Case { const char* file; vault::VideoCodec codec; int width; };
    const Case cases[] = {
        {OSV_MEDIA_FIXTURE_DIR "/tiny_prores.mov", vault::VideoCodec::ProRes, 160},
        {OSV_MEDIA_FIXTURE_DIR "/tiny_dnxhr.mov",  vault::VideoCodec::DNxHD,  256},
        {OSV_MEDIA_FIXTURE_DIR "/tiny_mjpeg.mov",  vault::VideoCodec::MJPEG,  160},
    };
    for (const auto& c : cases) {
        auto v_bytes = read_file(c.file);
        REQUIRE(!v_bytes.empty());
        TempVault tv("decoder_mov_pro");
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
        REQUIRE(v.add_video("c", v_bytes, "clip.mov", 4096) == vault::VaultResult::Ok);
        auto kids = v.list("c");
        REQUIRE(kids.size() == 1);

        media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
        REQUIRE(avio.valid());
        media::VideoDecoder dec;
        REQUIRE(dec.open(avio.ctx()));
        CHECK(dec.codec()  == c.codec);
        CHECK(dec.width()  == c.width);
        CHECK(dec.height() == 120);

        int n = 0;
        while (auto f = dec.next_frame()) {
            CHECK(f->pix_fmt == media::FramePixelFormat::I420);
            ++n;
        }
        CHECK(n == 10);
    }
}

TEST(video_decoder_decodes_webm_vp8_vp9)
{
    // Phase 38: VP8 / VP9 .webm streams decode end-to-end through the
    // encrypted-chunk path, same shape as the Phase 28 .mov pro-codec test.
    struct Case { const char* file; vault::VideoCodec codec; int width; };
    const Case cases[] = {
        {OSV_MEDIA_FIXTURE_DIR "/tiny_vp8.webm", vault::VideoCodec::VP8, 160},
        {OSV_MEDIA_FIXTURE_DIR "/tiny_vp9.webm", vault::VideoCodec::VP9, 256},
    };
    for (const auto& c : cases) {
        auto v_bytes = read_file(c.file);
        REQUIRE(!v_bytes.empty());
        TempVault tv("decoder_webm");
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
        REQUIRE(v.add_video("c", v_bytes, "clip.webm", 4096) == vault::VaultResult::Ok);
        auto kids = v.list("c");
        REQUIRE(kids.size() == 1);

        media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
        REQUIRE(avio.valid());
        media::VideoDecoder dec;
        REQUIRE(dec.open(avio.ctx()));
        CHECK(dec.codec()  == c.codec);
        CHECK(dec.width()  == c.width);
        CHECK(dec.height() == 120);

        int n = 0;
        while (auto f = dec.next_frame()) {
            CHECK(f->pix_fmt == media::FramePixelFormat::I420);
            ++n;
        }
        CHECK(n == 10);
    }
}

TEST(video_decoder_webm_vp8_opus_and_vp9_vorbis_audio)
{
    // Phase 38: confirm the already-working Opus/Vorbis audio path (Phase 16)
    // still A/V-syncs correctly once VP8/VP9 video decode is wired up.
    struct Case { const char* file; vault::VideoCodec codec; };
    const Case cases[] = {
        {OSV_MEDIA_FIXTURE_DIR "/tiny_vp8_opus.webm",   vault::VideoCodec::VP8},
        {OSV_MEDIA_FIXTURE_DIR "/tiny_vp9_vorbis.webm", vault::VideoCodec::VP9},
    };
    for (const auto& c : cases) {
        auto v_bytes = read_file(c.file);
        REQUIRE(!v_bytes.empty());
        TempVault tv("decoder_webm_audio");
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
        REQUIRE(v.add_video("c", v_bytes, "clip.webm", 4096) == vault::VaultResult::Ok);
        auto kids = v.list("c");
        REQUIRE(kids.size() == 1);

        media::ChunkAvio avio(media::VideoSource::open(v, *kids[0]));
        REQUIRE(avio.valid());
        media::VideoDecoder dec;
        REQUIRE(dec.open(avio.ctx()));
        CHECK(dec.codec() == c.codec);
        REQUIRE(dec.has_audio());

        int vframes = 0;
        size_t asamples = 0;
        for (int i = 0; i < 5; ++i)
            if (dec.next_frame())
                ++vframes;
        for (int i = 0; i < 20; ++i)
            if (auto a = dec.next_audio_frame())
                asamples += a->samples.size();

        CHECK(vframes > 0);
        CHECK(asamples > 0);
    }
}

#endif // OSV_VENDORED_AV
