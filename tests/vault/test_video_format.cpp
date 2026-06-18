#include "test_framework.h"
#include <cstdint>
#include <vector>
#include "vault/video_format.h"

namespace {
std::vector<uint8_t> with_prefix(std::vector<uint8_t> head, size_t total = 32)
{
    head.resize(total, 0);
    return head;
}
} // namespace

TEST(detect_mp4_by_ftyp_box)
{
    // bytes 4..7 == "ftyp" (an ISO-BMFF / MP4 / MOV file-type box).
    std::vector<uint8_t> d = {0,0,0,0x18, 'f','t','y','p', 'i','s','o','m'};
    CHECK_EQ(static_cast<int>(vault::detect_video_container(with_prefix(d))),
             static_cast<int>(vault::VideoContainer::MP4));
}

TEST(detect_matroska_by_ebml_magic)
{
    // EBML magic 0x1A45DFA3 at offset 0 (Matroska / WebM).
    std::vector<uint8_t> d = {0x1A,0x45,0xDF,0xA3, 'a','b','c'};
    CHECK_EQ(static_cast<int>(vault::detect_video_container(with_prefix(d))),
             static_cast<int>(vault::VideoContainer::MKV));
}

TEST(detect_unknown_for_non_video)
{
    std::vector<uint8_t> jpeg = {0xFF,0xD8,0xFF,0xE0, 'J','F','I','F'};
    CHECK_EQ(static_cast<int>(vault::detect_video_container(with_prefix(jpeg))),
             static_cast<int>(vault::VideoContainer::Unknown));
    std::vector<uint8_t> tiny = {0x00, 0x01};      // too short
    CHECK_EQ(static_cast<int>(vault::detect_video_container(tiny)),
             static_cast<int>(vault::VideoContainer::Unknown));
    CHECK_EQ(static_cast<int>(vault::detect_video_container({})),
             static_cast<int>(vault::VideoContainer::Unknown));
}
