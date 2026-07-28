#include "test_framework.h"

#include <cstdint>
#include <span>
#include <vector>

#include "image/anim_info.h"
#include "image/fixtures.h"

namespace {

// Minimal GIF89a scaffolding: header + logical screen descriptor with a
// 2-entry global colour table, then caller-supplied block bytes + trailer.
std::vector<uint8_t> gif_with(const std::vector<uint8_t>& blocks)
{
    std::vector<uint8_t> g = {
        'G', 'I', 'F', '8', '9', 'a',
        0x02, 0x00,             // width  = 2
        0x02, 0x00,             // height = 2
        0xF0,                   // GCT present, 2 entries
        0x00,                   // background colour index
        0x00,                   // pixel aspect ratio
        0xFF, 0x00, 0x00,       // GCT[0]
        0x00, 0x00, 0xFF,       // GCT[1]
    };
    g.insert(g.end(), blocks.begin(), blocks.end());
    g.push_back(0x3B);          // trailer
    return g;
}

// One complete frame: graphic control extension + image descriptor +
// LZW-compressed 2x2 image of colour index 0.
const std::vector<uint8_t> kFrame = {
    0x21, 0xF9, 0x04, 0x00, 0x0A, 0x00, 0x00, 0x00,   // GCE, delay = 10 (100ths)
    0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00,  // image descriptor
    0x02,                     // LZW minimum code size
    0x03, 0x04, 0x80, 0x02,   // one data sub-block
    0x00,                     // block terminator
};

std::vector<uint8_t> frames(int n)
{
    std::vector<uint8_t> out;
    for (int i = 0; i < n; ++i) {
        out.insert(out.end(), kFrame.begin(), kFrame.end());
    }
    return out;
}

} // namespace

TEST(gif_info_single_frame_is_not_animated)
{
    const auto g = gif_with(frames(1));
    CHECK(!image::gif_is_animated(g));
}

TEST(gif_info_two_frames_is_animated)
{
    const auto g = gif_with(frames(2));
    CHECK(image::gif_is_animated(g));
}

TEST(gif_info_many_frames_is_animated)
{
    const auto g = gif_with(frames(5));
    CHECK(image::gif_is_animated(g));
}

TEST(gif_info_empty_input)
{
    CHECK(!image::gif_is_animated(std::span<const uint8_t>{}));
}

TEST(gif_info_non_gif_bytes)
{
    const std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    CHECK(!image::gif_is_animated(png));
}

TEST(gif_info_truncated_header)
{
    const std::vector<uint8_t> g = {'G', 'I', 'F', '8'};
    CHECK(!image::gif_is_animated(g));
}

TEST(gif_info_truncated_after_screen_descriptor)
{
    auto g = gif_with(frames(2));
    g.resize(13);   // header + LSD only, colour table cut off
    CHECK(!image::gif_is_animated(g));
}

TEST(gif_info_truncated_mid_second_frame)
{
    auto g = gif_with(frames(2));
    g.resize(g.size() - 8);   // second frame's data cut off
    // Either verdict is memory-safe; the point of this test is that it returns deterministically.
    const bool verdict1 = image::gif_is_animated(g);
    const bool verdict2 = image::gif_is_animated(g);
    CHECK(verdict1 == verdict2);
}

TEST(gif_info_unterminated_sub_block_chain)
{
    // A block size byte promising more bytes than the buffer holds.
    std::vector<uint8_t> blocks = {
        0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00,
        0x02, 0x7F,   // sub-block claims 127 bytes that do not exist
    };
    const auto g = gif_with(blocks);
    CHECK(!image::gif_is_animated(g));
}

TEST(gif_info_extension_blocks_are_skipped)
{
    // An application extension (e.g. NETSCAPE loop) before two frames must not
    // confuse the walker.
    std::vector<uint8_t> blocks = {
        0x21, 0xFF, 0x0B,
        'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0',
        0x03, 0x01, 0x00, 0x00,
        0x00,
    };
    const auto f = frames(2);
    blocks.insert(blocks.end(), f.begin(), f.end());
    const auto g = gif_with(blocks);
    CHECK(image::gif_is_animated(g));
}

// --- WebP animation detection (Phase 57) --------------------------------------
// The WebP probe delegates to libwebp's own header parser (WebPGetFeatures)
// rather than walking RIFF ourselves, so these tests pin the contract we rely on
// rather than a parser of our own: an animated file says yes, everything else —
// including hostile input — says no without reading past the buffer.

TEST(webp_is_animated_true_for_animated_fixture)
{
    const auto buf = fixtures::load_anim_webp();
    REQUIRE(!buf.empty());
    CHECK(image::webp_is_animated(buf));
}

TEST(webp_is_animated_true_for_animated_fixture_with_alpha)
{
    const auto buf = fixtures::load_anim_webp_alpha();
    REQUIRE(!buf.empty());
    CHECK(image::webp_is_animated(buf));
}

TEST(webp_is_animated_false_for_static_fixture)
{
    const auto buf = fixtures::load_webp();
    REQUIRE(!buf.empty());
    CHECK(!image::webp_is_animated(buf));
}

TEST(webp_is_animated_false_for_truncated_input)
{
    auto buf = fixtures::load_anim_webp();
    REQUIRE(buf.size() > 20);
    buf.resize(20);                 // RIFF/VP8X header only, no frame data
    CHECK(!image::webp_is_animated(buf));
}

TEST(webp_is_animated_false_for_empty_and_garbage)
{
    CHECK(!image::webp_is_animated({}));

    // "RIFF" with a non-WEBP form type must be rejected, not probed further.
    const std::vector<uint8_t> not_webp{'R', 'I', 'F', 'F', 4, 0, 0, 0, 'A', 'V', 'I', ' '};
    CHECK(!image::webp_is_animated(not_webp));

    const std::vector<uint8_t> noise(64, 0xAB);
    CHECK(!image::webp_is_animated(noise));
}

TEST(is_animated_dispatches_on_format)
{
    const auto webp_anim  = fixtures::load_anim_webp();
    const auto webp_still = fixtures::load_webp();
    REQUIRE(!webp_anim.empty());
    REQUIRE(!webp_still.empty());

    CHECK(image::is_animated(image::ImageFormat::WebP, webp_anim));
    CHECK(!image::is_animated(image::ImageFormat::WebP, webp_still));

    // A format that cannot animate is never probed, whatever the bytes hold.
    CHECK(!image::is_animated(image::ImageFormat::PNG, webp_anim));
    CHECK(!image::is_animated(image::ImageFormat::JPEG, webp_anim));
}

TEST(is_animated_routes_gif_to_the_gif_walker)
{
    const auto g = gif_with(frames(2));
    CHECK(image::is_animated(image::ImageFormat::GIF, g));
    CHECK(image::gif_is_animated(g));

    const auto one = gif_with(frames(1));
    CHECK(!image::is_animated(image::ImageFormat::GIF, one));
}
