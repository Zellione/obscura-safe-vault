#include "test_framework.h"

#ifdef OSV_VENDORED_AV

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "media/video_probe.h"
#include "image/image.h"
#include "image/decode.h"

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

TEST(probe_video_rejects_garbage_data)
{
    std::vector<uint8_t> junk(8192, 0);

    media::VideoProbeResult result;
    CHECK(!media::probe_video(std::span(junk), result));
}

#endif // OSV_VENDORED_AV
