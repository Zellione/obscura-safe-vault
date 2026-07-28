#include "test_framework.h"

// WebpAnimDecoder is deliberately NOT gated on OSV_VENDORED_AV: libwebp is a
// hard dependency of the app, so an animated WebP plays even in a build without
// vendored FFmpeg (where GIF animation is unavailable).

#include <memory>
#include <vector>

#include "image/fixtures.h"
#include "media/anim_decoder.h"
#include "media/webp_anim_decoder.h"

TEST(webp_frame_delay_converts_cumulative_timestamps)
{
    // libwebp reports cumulative ms; playback wants a per-frame delay in seconds.
    CHECK(media::webp_frame_delay_s(100, 200) > 0.099);
    CHECK(media::webp_frame_delay_s(100, 200) < 0.101);
    CHECK(media::webp_frame_delay_s(0, 1000) > 0.999);
    CHECK(media::webp_frame_delay_s(0, 1000) < 1.001);
}

TEST(webp_frame_delay_clamps_to_the_minimum)
{
    CHECK_EQ(media::webp_frame_delay_s(100, 100), media::kMinFrameDelay);  // 0 ms declared
    CHECK_EQ(media::webp_frame_delay_s(100,  90), media::kMinFrameDelay);  // non-monotonic
    CHECK_EQ(media::webp_frame_delay_s(0,    10), media::kMinFrameDelay);  // under the floor
}

TEST(webp_anim_decoder_opens_animated_fixture)
{
    const auto bytes = fixtures::load_anim_webp();
    REQUIRE(!bytes.empty());

    media::WebpAnimDecoder d;
    REQUIRE(d.open(bytes));
    CHECK_EQ(d.width(),  8);
    CHECK_EQ(d.height(), 8);
}

TEST(webp_anim_decoder_rejects_static_and_garbage_input)
{
    const auto static_webp = fixtures::load_webp();
    REQUIRE(!static_webp.empty());

    media::WebpAnimDecoder still;
    CHECK(!still.open(static_webp));      // a single frame is not an animation

    media::WebpAnimDecoder junk;
    const std::vector<uint8_t> garbage(64, 0xAB);
    CHECK(!junk.open(garbage));

    media::WebpAnimDecoder empty;
    CHECK(!empty.open({}));

    // A decoder that never opened must not hand out frames.
    CHECK(!empty.next_frame().has_value());
    CHECK_EQ(empty.width(), 0);
}

TEST(webp_anim_decoder_survives_a_truncated_animation)
{
    auto bytes = fixtures::load_anim_webp();
    REQUIRE(bytes.size() > 40);
    bytes.resize(bytes.size() / 2);       // cut mid-animation

    media::WebpAnimDecoder d;
    // Either open() rejects it or frame decoding stops early — never a crash,
    // never a frame whose buffer disagrees with its dimensions.
    if (d.open(bytes)) {
        while (auto f = d.next_frame()) {
            CHECK_EQ(f->rgba.size(),
                     static_cast<size_t>(f->width) * static_cast<size_t>(f->height) * 4);
        }
    }
    CHECK(true);
}

TEST(webp_anim_decoder_yields_every_frame_with_delays)
{
    const auto bytes = fixtures::load_anim_webp();
    REQUIRE(!bytes.empty());

    media::WebpAnimDecoder d;
    REQUIRE(d.open(bytes));

    size_t n = 0;
    while (auto f = d.next_frame()) {
        CHECK_EQ(f->width,  8);
        CHECK_EQ(f->height, 8);
        CHECK_EQ(f->rgba.size(), static_cast<size_t>(8 * 8 * 4));
        CHECK(f->delay_s >= media::kMinFrameDelay);
        // The fixture declares 100 ms per frame.
        CHECK(f->delay_s > 0.099);
        CHECK(f->delay_s < 0.101);
        ++n;
    }
    CHECK_EQ(n, static_cast<size_t>(3));
    CHECK_EQ(d.frames_decoded(), static_cast<size_t>(3));
}

TEST(webp_anim_decoder_frames_carry_their_own_colours)
{
    // Frames 0..2 are solid #3366cc, #cc6633, #33cc66 (lossless, so exact).
    const auto bytes = fixtures::load_anim_webp();
    REQUIRE(!bytes.empty());

    media::WebpAnimDecoder d;
    REQUIRE(d.open(bytes));

    const auto f0 = d.next_frame();
    REQUIRE(f0.has_value());
    CHECK_EQ(f0->rgba[0], 0x33);
    CHECK_EQ(f0->rgba[1], 0x66);
    CHECK_EQ(f0->rgba[2], 0xcc);
    CHECK_EQ(f0->rgba[3], 0xFF);

    const auto f1 = d.next_frame();
    REQUIRE(f1.has_value());
    CHECK_EQ(f1->rgba[0], 0xcc);
    CHECK_EQ(f1->rgba[1], 0x66);
    CHECK_EQ(f1->rgba[2], 0x33);
}

TEST(webp_anim_decoder_rewind_replays_the_animation)
{
    const auto bytes = fixtures::load_anim_webp();
    REQUIRE(!bytes.empty());

    media::WebpAnimDecoder d;
    REQUIRE(d.open(bytes));

    while (d.next_frame()) { }
    CHECK(!d.next_frame().has_value());   // exhausted

    d.rewind();
    CHECK_EQ(d.frames_decoded(), static_cast<size_t>(0));

    size_t n = 0;
    while (d.next_frame()) { ++n; }
    CHECK_EQ(n, static_cast<size_t>(3));
}

TEST(webp_anim_decoder_emits_opaque_pixels_for_transparent_frames)
{
    const auto bytes = fixtures::load_anim_webp_alpha();
    REQUIRE(!bytes.empty());

    media::WebpAnimDecoder d;
    REQUIRE(d.open(bytes));

    const auto first = d.next_frame();
    REQUIRE(first.has_value());
    // Frame 0 is fully transparent: flattened over black it is opaque (0,0,0,255).
    // Every byte of the frame must be, not just the first pixel — a partial
    // flatten would leak whatever libwebp left in the canvas.
    for (size_t px = 0; px < static_cast<size_t>(8 * 8); ++px) {
        CHECK_EQ(first->rgba[(px * 4) + 0], 0x00);
        CHECK_EQ(first->rgba[(px * 4) + 1], 0x00);
        CHECK_EQ(first->rgba[(px * 4) + 2], 0x00);
        CHECK_EQ(first->rgba[(px * 4) + 3], 0xFF);
    }
}

TEST(webp_anim_decoder_reopen_resets_state)
{
    const auto bytes = fixtures::load_anim_webp();
    REQUIRE(!bytes.empty());

    media::WebpAnimDecoder d;
    REQUIRE(d.open(bytes));
    REQUIRE(d.next_frame().has_value());
    CHECK_EQ(d.frames_decoded(), static_cast<size_t>(1));

    REQUIRE(d.open(bytes));               // same decoder, fresh animation
    CHECK_EQ(d.frames_decoded(), static_cast<size_t>(0));
    CHECK_EQ(d.width(), 8);
}

TEST(webp_anim_decoder_is_usable_through_the_anim_decoder_interface)
{
    const auto bytes = fixtures::load_anim_webp();
    REQUIRE(!bytes.empty());

    std::unique_ptr<media::AnimDecoder> d = std::make_unique<media::WebpAnimDecoder>();
    REQUIRE(d->open(bytes));
    CHECK_EQ(d->width(),  8);
    CHECK_EQ(d->height(), 8);
    REQUIRE(d->next_frame().has_value());
    d->rewind();
    CHECK_EQ(d->frames_decoded(), static_cast<size_t>(0));
}
