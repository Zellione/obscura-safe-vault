#include "test_framework.h"

#ifdef OSV_VENDORED_AV

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "media/video_probe.h"
#include "image/image.h"
#include "image/decode.h"

// For Phase 85 MPEG-PS fixture decoding (uudecode).
#ifdef OSV_VENDORED_ARCHIVE
#include "ui/archive_test_helpers.h"
#endif

namespace {

// Read a file into a vector.
std::vector<uint8_t> read_file(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

} // namespace

TEST(probe_video_mp4_succeeds_and_fills_metadata)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    media::VideoProbeResult result;
    REQUIRE(media::probe_video(std::span(video_bytes), result));

    CHECK_EQ(static_cast<int>(result.container), static_cast<int>(vault::VideoContainer::MP4));
    CHECK_EQ(static_cast<int>(result.codec), static_cast<int>(vault::VideoCodec::H264));
    CHECK_EQ(result.width, 160);
    CHECK_EQ(result.height, 120);
    CHECK(result.duration_us > 0);
    CHECK(!result.poster_jpeg.empty());

    // Verify the poster decodes successfully.
    auto poster_data = image::decode_from_memory(std::span(result.poster_jpeg));
    REQUIRE(poster_data.has_value());
    CHECK(poster_data->width <= 256);
    CHECK(poster_data->height <= 256);
}

TEST(probe_video_mov_pro_codecs_fill_metadata_and_poster)
{
    // Phase 28: codecs common in .mov beyond H.264/H.265 — ProRes, DNxHR
    // (FFmpeg's dnxhd codec id), MJPEG — probe, fill metadata, make a poster.
    struct Case { const char* file; vault::VideoCodec codec; uint32_t width; };
    const Case cases[] = {
        {OSV_MEDIA_FIXTURE_DIR "/tiny_prores.mov", vault::VideoCodec::ProRes, 160u},
        {OSV_MEDIA_FIXTURE_DIR "/tiny_dnxhr.mov",  vault::VideoCodec::DNxHD,  256u},
        {OSV_MEDIA_FIXTURE_DIR "/tiny_mjpeg.mov",  vault::VideoCodec::MJPEG,  160u},
    };
    for (const auto& c : cases) {
        auto video_bytes = read_file(c.file);
        REQUIRE(!video_bytes.empty());

        media::VideoProbeResult result;
        REQUIRE(media::probe_video(std::span(video_bytes), result));

        // .mov is an ftyp/ISO-BMFF container — same MP4 path as .mp4.
        CHECK_EQ(static_cast<int>(result.container), static_cast<int>(vault::VideoContainer::MP4));
        CHECK_EQ(static_cast<int>(result.codec), static_cast<int>(c.codec));
        CHECK_EQ(result.width, c.width);
        CHECK_EQ(result.height, 120u);
        CHECK(result.duration_us > 0);
        CHECK(!result.poster_jpeg.empty());

        auto poster_data = image::decode_from_memory(std::span(result.poster_jpeg));
        REQUIRE(poster_data.has_value());
    }
}

TEST(probe_video_webm_vp8_vp9_fill_metadata_and_poster)
{
    // Phase 38: WebM's own codec pair, VP8 and VP9, each with and without an
    // Opus/Vorbis audio track.
    struct Case { const char* file; vault::VideoCodec codec; uint32_t width; };
    const Case cases[] = {
        {OSV_MEDIA_FIXTURE_DIR "/tiny_vp8.webm",        vault::VideoCodec::VP8, 160u},
        {OSV_MEDIA_FIXTURE_DIR "/tiny_vp8_opus.webm",   vault::VideoCodec::VP8, 160u},
        {OSV_MEDIA_FIXTURE_DIR "/tiny_vp9.webm",        vault::VideoCodec::VP9, 256u},
        {OSV_MEDIA_FIXTURE_DIR "/tiny_vp9_vorbis.webm", vault::VideoCodec::VP9, 256u},
    };
    for (const auto& c : cases) {
        auto video_bytes = read_file(c.file);
        REQUIRE(!video_bytes.empty());

        media::VideoProbeResult result;
        REQUIRE(media::probe_video(std::span(video_bytes), result));

        // .webm is EBML/Matroska — the shared MKV container path.
        CHECK_EQ(static_cast<int>(result.container), static_cast<int>(vault::VideoContainer::MKV));
        CHECK_EQ(static_cast<int>(result.codec), static_cast<int>(c.codec));
        CHECK_EQ(result.width, c.width);
        CHECK_EQ(result.height, 120u);
        CHECK(result.duration_us > 0);
        CHECK(!result.poster_jpeg.empty());

        auto poster_data = image::decode_from_memory(std::span(result.poster_jpeg));
        REQUIRE(poster_data.has_value());
    }
}

TEST(probe_video_mov_legacy_codecs_fill_metadata_and_poster)
{
    // Phase 40: legacy .mov codecs beyond the Phase 28 set — QuickTime
    // Animation/RLE and Cinepak.
    struct Case { const char* file; vault::VideoCodec codec; uint32_t width; };
    const Case cases[] = {
        {OSV_MEDIA_FIXTURE_DIR "/tiny_qtrle.mov",   vault::VideoCodec::QTRLE,   160u},
        {OSV_MEDIA_FIXTURE_DIR "/tiny_cinepak.mov", vault::VideoCodec::Cinepak, 160u},
    };
    for (const auto& c : cases) {
        auto video_bytes = read_file(c.file);
        REQUIRE(!video_bytes.empty());

        media::VideoProbeResult result;
        REQUIRE(media::probe_video(std::span(video_bytes), result));

        CHECK_EQ(static_cast<int>(result.container), static_cast<int>(vault::VideoContainer::MP4));
        CHECK_EQ(static_cast<int>(result.codec), static_cast<int>(c.codec));
        CHECK_EQ(result.width, c.width);
        CHECK_EQ(result.height, 120u);
        CHECK(result.duration_us > 0);
        CHECK(!result.poster_jpeg.empty());

        auto poster_data = image::decode_from_memory(std::span(result.poster_jpeg));
        REQUIRE(poster_data.has_value());
    }
}

TEST(probe_video_webm_av1_fills_metadata_and_poster)
{
    // Phase 40: AV1's own .webm entry (the codec pair's third member alongside
    // VP8/VP9, Phase 38's WebM out-of-scope note explicitly deferred this).
    auto video_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_av1.webm");
    REQUIRE(!video_bytes.empty());

    media::VideoProbeResult result;
    REQUIRE(media::probe_video(std::span(video_bytes), result));

    CHECK_EQ(static_cast<int>(result.container), static_cast<int>(vault::VideoContainer::MKV));
    CHECK_EQ(static_cast<int>(result.codec), static_cast<int>(vault::VideoCodec::AV1));
    CHECK_EQ(result.width, 256u);
    CHECK_EQ(result.height, 120u);
    CHECK(result.duration_us > 0);
    CHECK(!result.poster_jpeg.empty());

    auto poster_data = image::decode_from_memory(std::span(result.poster_jpeg));
    REQUIRE(poster_data.has_value());
}

// Phase 80 regression: a video whose width (106, mod 16 = 10) puts swscale's
// final RGB24 vector store past a tight buffer end. decode_poster_rgb used to
// hand sws_scale an exactly-sized align=1 destination; libswscale's vectorized
// writers store whole vectors per row (measured up to 42 bytes past the end),
// which corrupted the heap — 0xc0000374 on Windows, where av_malloc hides its
// base pointer in the inter-block gap. ASAN cannot see the overrun (the store
// happens in uninstrumented vendored asm); valgrind or the Phase 80 canary
// sweep are the tools that catch this class.
TEST(probe_video_odd_stride_poster_stays_in_bounds)
{
    auto video_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_oddstride.mp4");
    REQUIRE(!video_bytes.empty());

    media::VideoProbeResult result;
    REQUIRE(media::probe_video(std::span(video_bytes), result));
    CHECK_EQ(static_cast<int>(result.codec), static_cast<int>(vault::VideoCodec::H264));
    CHECK_EQ(result.width, 106);
    CHECK_EQ(result.height, 64);
    REQUIRE(!result.poster_jpeg.empty());

    // The poster must decode and preserve the odd-stride dims (no row shear).
    auto poster = image::decode_from_memory(std::span(result.poster_jpeg));
    REQUIRE(poster.has_value());
    CHECK_EQ(poster->width, 106);
    CHECK_EQ(poster->height, 64);
}

TEST(probe_video_rejects_garbage_data)
{
    std::vector<uint8_t> junk(8192, 0);

    media::VideoProbeResult result;
    CHECK(!media::probe_video(std::span(junk), result));
}

#ifdef OSV_VENDORED_ARCHIVE
TEST(probe_video_mpeg_ps_identifies_codec)
{
    // Phase 85: raw MPEG-PS needs the FFmpeg `mpegvideo` probe demuxer
    // (mpegps defers video codec identification to codec probing).
    namespace fs = std::filesystem;
    const auto fixture_dir = fs::path(OSV_MEDIA_FIXTURE_DIR);
    const auto temp_dir = fs::temp_directory_path();

    // Decode MPEG-2 PS fixture (uuencoded to keep it checked in)
    std::vector<uint8_t> ps2 = archivetest::uudecode(
        fixture_dir / "mpeg2_ps.mpg.uu",
        temp_dir / "mpeg2_ps.mpg");
    REQUIRE(!ps2.empty());

    media::VideoProbeResult r2;
    REQUIRE(media::probe_video(std::span(ps2), r2));
    CHECK_EQ(static_cast<int>(r2.container), static_cast<int>(vault::VideoContainer::MPEGPS));
    CHECK_EQ(static_cast<int>(r2.codec), static_cast<int>(vault::VideoCodec::MPEG2));
    CHECK_EQ(r2.width, 96u);

    // FFmpeg's raw-ES probe initially tags MPEG-1-in-PS with the MPEG-2 codec
    // id (demux.c: "mpegvideo" -> AV_CODEC_ID_MPEG2VIDEO), but stream-info
    // frame parsing then refines it to MPEG-1 — verified empirically against
    // this build.
    std::vector<uint8_t> ps1 = archivetest::uudecode(
        fixture_dir / "mpeg1_ps.mpg.uu",
        temp_dir / "mpeg1_ps.mpg");
    REQUIRE(!ps1.empty());

    media::VideoProbeResult r1;
    REQUIRE(media::probe_video(std::span(ps1), r1));
    CHECK_EQ(static_cast<int>(r1.container), static_cast<int>(vault::VideoContainer::MPEGPS));
    CHECK_EQ(static_cast<int>(r1.codec), static_cast<int>(vault::VideoCodec::MPEG1));
    CHECK_EQ(r1.width, 96u);
}
#endif // OSV_VENDORED_ARCHIVE

#endif // OSV_VENDORED_AV
