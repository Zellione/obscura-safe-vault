#include "test_framework.h"

#ifdef OSV_VENDORED_AV

#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "crypto/secure_mem.h"
#include "media/anim_decoder.h"
#include "media/gif_decoder.h"

namespace {

std::vector<uint8_t> read_fixture(const char* name)
{
    const std::string path = std::string(OSV_MEDIA_FIXTURE_DIR) + "/" + name;
    std::vector<uint8_t> out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return out;
    }
    std::array<uint8_t, 4096> buf;
    size_t n = 0;
    while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
        out.insert(out.end(), buf.begin(), buf.begin() + n);
    }
    std::fclose(f);
    return out;
}

} // namespace

TEST(gif_decoder_opens_animated_fixture)
{
    const auto bytes = read_fixture("tiny_anim.gif");
    REQUIRE(!bytes.empty());

    media::GifDecoder d;
    REQUIRE(d.open(bytes));
    CHECK(d.width()  > 0);
    CHECK(d.height() > 0);
}

TEST(gif_decoder_yields_multiple_rgba_frames)
{
    const auto bytes = read_fixture("tiny_anim.gif");
    REQUIRE(!bytes.empty());

    media::GifDecoder d;
    REQUIRE(d.open(bytes));

    size_t n = 0;
    while (auto f = d.next_frame()) {
        CHECK_EQ(f->width,  d.width());
        CHECK_EQ(f->height, d.height());
        CHECK_EQ(f->rgba.size(),
                 static_cast<size_t>(f->width) * static_cast<size_t>(f->height) * 4);
        CHECK(f->delay_s >= 0.02);   // the 20 ms floor
        ++n;
    }
    CHECK(n >= 2);
}

TEST(gif_decoder_rgba_frame_is_locked_and_wiped)
{
    const auto bytes = read_fixture("tiny_anim.gif");
    REQUIRE(!bytes.empty());
    media::GifDecoder d;
    REQUIRE(d.open(bytes));

    crypto::detail::reset_wipe_observations_for_tests();
    const auto before = crypto::detail::wiping_deallocation_count();
    {
        auto frame = d.next_frame();
        REQUIRE(frame.has_value());
        REQUIRE(!frame->rgba.empty());
        CHECK(crypto::detail::locked_page_refcount_for_tests(frame->rgba.data()) > 0);
    }
    CHECK(crypto::detail::wiping_deallocation_count() > before);
    CHECK(crypto::detail::all_wipe_observations_zero_for_tests());
}

TEST(gif_decoder_rewind_replays_the_same_frame_count)
{
    const auto bytes = read_fixture("tiny_anim.gif");
    REQUIRE(!bytes.empty());

    media::GifDecoder d;
    REQUIRE(d.open(bytes));

    size_t first = 0;
    while (d.next_frame()) {
        ++first;
    }
    CHECK(first >= 2);

    d.rewind();

    size_t second = 0;
    while (d.next_frame()) {
        ++second;
    }
    CHECK_EQ(second, first);
}

TEST(gif_decoder_rejects_garbage)
{
    const std::vector<uint8_t> junk(512, 0xAB);
    media::GifDecoder d;
    CHECK(!d.open(junk));
}

TEST(gif_decoder_rejects_truncated_gif)
{
    auto bytes = read_fixture("tiny_anim.gif");
    REQUIRE(bytes.size() > 32);
    bytes.resize(24);

    media::GifDecoder d;
    // Opening may succeed or fail; decoding must not crash or hang.
    if (d.open(bytes)) {
        while (d.next_frame()) {
            // Drain frames without crashing
        }
    }
    CHECK(true);
}

TEST(gif_decoder_reports_real_frame_delays)
{
    const auto bytes = read_fixture("tiny_anim.gif");
    REQUIRE(!bytes.empty());

    media::GifDecoder d;
    REQUIRE(d.open(bytes));

    const double expected_delay = 0.25;  // tiny_anim.gif: 25 ticks @ 1/100 time_base
    const double tolerance = 0.01;       // Allow ±0.01s margin

    size_t frame_count = 0;
    while (auto f = d.next_frame()) {
        CHECK(f->delay_s >= expected_delay - tolerance);
        CHECK(f->delay_s <= expected_delay + tolerance);
        ++frame_count;
    }
    // tiny_anim.gif has 4 frames
    CHECK(frame_count == 4);
}

// Phase 81: sws_scale targets AV_PIX_FMT_RGBA, so a decoded frame's bytes are
// R,G,B,A in *memory order* — the contract AnimPlayback's texture format has to
// match. Pinning it here means a future pixel-format change in the decoder can
// no longer silently mismatch the upload. tiny_anim.gif's first frame has a
// green run at (8,0) and a yellow centre; both are exact palette colours.
TEST(gif_decoder_frames_are_byte_order_rgba)
{
    const auto bytes = read_fixture("tiny_anim.gif");
    REQUIRE(!bytes.empty());

    media::GifDecoder d;
    REQUIRE(d.open(bytes));

    const auto f = d.next_frame();
    REQUIRE(f.has_value());
    REQUIRE(f->width == 32);
    REQUIRE(f->height == 32);

    const auto px = [&f](int x, int y) {
        return f->rgba.data() + ((static_cast<size_t>(y) * 32 + static_cast<size_t>(x)) * 4);
    };

    const uint8_t* green = px(8, 0);
    CHECK_EQ(green[0], 0);      // R
    CHECK_EQ(green[1], 252);    // G
    CHECK_EQ(green[2], 0);      // B
    CHECK_EQ(green[3], 255);    // A

    const uint8_t* yellow = px(16, 16);
    CHECK_EQ(yellow[0], 252);
    CHECK_EQ(yellow[1], 252);
    CHECK_EQ(yellow[2], 0);
    CHECK_EQ(yellow[3], 255);
}

// Phase 57: GifDecoder is one backend behind media::AnimDecoder, so playback can
// drive it and WebpAnimDecoder through the same handle.
TEST(gif_decoder_is_usable_through_the_anim_decoder_interface)
{
    const auto bytes = read_fixture("tiny_anim.gif");
    REQUIRE(!bytes.empty());

    std::unique_ptr<media::AnimDecoder> d = std::make_unique<media::GifDecoder>();
    REQUIRE(d->open(bytes));
    CHECK(d->width()  > 0);
    CHECK(d->height() > 0);

    const auto first = d->next_frame();
    REQUIRE(first.has_value());
    CHECK_EQ(first->rgba.size(),
             static_cast<size_t>(first->width) * static_cast<size_t>(first->height) * 4);
    CHECK(first->delay_s >= media::kMinFrameDelay);

    d->rewind();
    CHECK_EQ(d->frames_decoded(), static_cast<size_t>(0));
}

#endif // OSV_VENDORED_AV
