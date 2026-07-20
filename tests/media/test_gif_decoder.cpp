#include "test_framework.h"

#ifdef OSV_VENDORED_AV

#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <vector>

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
    uint8_t buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        out.insert(out.end(), buf, buf + n);
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
        }
    }
    CHECK(true);
}

#endif // OSV_VENDORED_AV
