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

TEST(probe_video_rejects_garbage_data)
{
    std::vector<uint8_t> junk(8192, 0);

    media::VideoProbeResult result;
    CHECK(!media::probe_video(std::span(junk), result));
}

#endif // OSV_VENDORED_AV
