#include "test_framework.h"

#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include "ui/dup_video_sig.h"

namespace fs = std::filesystem;

static std::vector<uint8_t> load_fixture(const char* name)
{
    std::ifstream f(fs::path(OSV_MEDIA_FIXTURE_DIR) / name, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// Byte reader over an in-memory buffer, matching the DupStreamRead contract.
static ui::DupStreamRead mem_reader(const std::vector<uint8_t>& buf)
{
    return [&buf](uint64_t offset, std::span<uint8_t> dst) -> int64_t {
        if (offset >= buf.size()) return 0;
        const size_t n = std::min(dst.size(), buf.size() - static_cast<size_t>(offset));
        std::memcpy(dst.data(), buf.data() + offset, n);
        return static_cast<int64_t>(n);
    };
}

TEST(video_frame_sig_garbage_input_fails_cleanly)
{
    const std::vector<uint8_t> junk(4096, 0x5A);
    ui::VideoSig s;
    CHECK(!ui::compute_video_frame_sig(mem_reader(junk), junk.size(), s));
    CHECK_EQ(s.frame_valid, uint8_t{0});
}

#ifdef OSV_VENDORED_AV

TEST(video_frame_sig_same_content_two_codecs_match)
{
    const auto a = load_fixture("dup_a_h264.mp4");
    const auto b = load_fixture("dup_a_vp9.webm");
    REQUIRE(!a.empty());
    REQUIRE(!b.empty());
    ui::VideoSig sa;
    ui::VideoSig sb;
    REQUIRE(ui::compute_video_frame_sig(mem_reader(a), a.size(), sa));
    REQUIRE(ui::compute_video_frame_sig(mem_reader(b), b.size(), sb));
    CHECK(std::popcount(sa.frame_valid) >= ui::DUP_VID_MIN_MATCHED);
    CHECK(ui::video_sig_match(sa, sb));
}

TEST(video_frame_sig_different_content_no_match)
{
    const auto a = load_fixture("dup_a_h264.mp4");
    const auto c = load_fixture("dup_other.mp4");
    REQUIRE(!a.empty());
    REQUIRE(!c.empty());
    ui::VideoSig sa;
    ui::VideoSig sc;
    REQUIRE(ui::compute_video_frame_sig(mem_reader(a), a.size(), sa));
    REQUIRE(ui::compute_video_frame_sig(mem_reader(c), c.size(), sc));
    CHECK(!ui::video_sig_match(sa, sc));
}

#endif // OSV_VENDORED_AV
