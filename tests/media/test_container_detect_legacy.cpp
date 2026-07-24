#include "test_framework.h"

#include <cstdint>
#include <vector>

#include "vault/video_format.h"

namespace {

// Helper to create a test buffer with a specific magic signature at offset 0.
std::vector<uint8_t> make_buffer_with_signature(std::initializer_list<uint8_t> sig,
                                                size_t total_size = 512)
{
    std::vector<uint8_t> buf(total_size, 0);
    size_t i = 0;
    for (auto byte : sig) {
        if (i < buf.size()) {
            buf[i++] = byte;
        }
    }
    return buf;
}

} // namespace

TEST(detect_container_avi_with_riff_and_avi_markers)
{
    // AVI: "RIFF" at offset 0, "AVI " at offset 8
    std::vector<uint8_t> buf(512, 0);
    buf[0] = 'R';
    buf[1] = 'I';
    buf[2] = 'F';
    buf[3] = 'F';
    buf[8] = 'A';
    buf[9] = 'V';
    buf[10] = 'I';
    buf[11] = ' ';

    auto result = vault::detect_video_container(std::span(buf));
    CHECK_EQ(static_cast<int>(result), static_cast<int>(vault::VideoContainer::AVI));
}

TEST(detect_container_mpeg_ps_with_start_code)
{
    // MPEG-PS: 0x00 0x00 0x01 0xBA at offset 0
    std::vector<uint8_t> buf = make_buffer_with_signature({0x00, 0x00, 0x01, 0xBA});
    auto result = vault::detect_video_container(std::span(buf));
    CHECK_EQ(static_cast<int>(result), static_cast<int>(vault::VideoContainer::MPEGPS));
}

TEST(detect_container_mpeg_ts_with_three_syncs)
{
    // MPEG-TS: 0x47 at offsets 0, 188, and 376
    std::vector<uint8_t> buf(512, 0);
    buf[0] = 0x47;
    buf[188] = 0x47;
    buf[376] = 0x47;

    auto result = vault::detect_video_container(std::span(buf));
    CHECK_EQ(static_cast<int>(result), static_cast<int>(vault::VideoContainer::MPEGTS));
}

TEST(detect_mpegts_requires_three_syncs)
{
    // Single 0x47 at offset 0 only should NOT be MPEGTS (weak signature)
    std::vector<uint8_t> buf(512, 0);
    buf[0] = 0x47;

    auto result = vault::detect_video_container(std::span(buf));
    CHECK_EQ(static_cast<int>(result), static_cast<int>(vault::VideoContainer::Unknown));
}

TEST(detect_container_asf_wmv_with_guid)
{
    // ASF/WMV: 16-byte GUID at offset 0
    // 30 26 B2 75 8E 66 CF 11 A6 D9 00 AA 00 62 CE 6C
    std::vector<uint8_t> buf(512, 0);
    buf[0] = 0x30;
    buf[1] = 0x26;
    buf[2] = 0xB2;
    buf[3] = 0x75;
    buf[4] = 0x8E;
    buf[5] = 0x66;
    buf[6] = 0xCF;
    buf[7] = 0x11;
    buf[8] = 0xA6;
    buf[9] = 0xD9;
    buf[10] = 0x00;
    buf[11] = 0xAA;
    buf[12] = 0x00;
    buf[13] = 0x62;
    buf[14] = 0xCE;
    buf[15] = 0x6C;

    auto result = vault::detect_video_container(std::span(buf));
    CHECK_EQ(static_cast<int>(result), static_cast<int>(vault::VideoContainer::ASF));
}

TEST(detect_container_flv_with_signature)
{
    // FLV: "FLV" at offset 0
    std::vector<uint8_t> buf = make_buffer_with_signature({'F', 'L', 'V'});
    auto result = vault::detect_video_container(std::span(buf));
    CHECK_EQ(static_cast<int>(result), static_cast<int>(vault::VideoContainer::FLV));
}

TEST(detect_container_ogg_with_oggsig)
{
    // Ogg: "OggS" at offset 0
    std::vector<uint8_t> buf = make_buffer_with_signature({'O', 'g', 'g', 'S'});
    auto result = vault::detect_video_container(std::span(buf));
    CHECK_EQ(static_cast<int>(result), static_cast<int>(vault::VideoContainer::OGG));
}

TEST(detect_container_realmedia_with_signature)
{
    // RealMedia: ".RMF" = 2E 52 4D 46 at offset 0
    std::vector<uint8_t> buf = make_buffer_with_signature({0x2E, 0x52, 0x4D, 0x46});
    auto result = vault::detect_video_container(std::span(buf));
    CHECK_EQ(static_cast<int>(result), static_cast<int>(vault::VideoContainer::RM));
}

TEST(detect_container_short_buffer_returns_unknown)
{
    // A 3-byte buffer starting with "RIF" should NOT match AVI (needs 12 bytes).
    // Bounds checking must prevent reading past the buffer end.
    std::vector<uint8_t> buf = {'R', 'I', 'F'};
    auto result = vault::detect_video_container(std::span(buf));
    CHECK_EQ(static_cast<int>(result), static_cast<int>(vault::VideoContainer::Unknown));
}

TEST(detect_container_unknown_bytes_returns_unknown)
{
    // Random garbage that doesn't match any signature
    std::vector<uint8_t> buf = {0xFF, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    auto result = vault::detect_video_container(std::span(buf));
    CHECK_EQ(static_cast<int>(result), static_cast<int>(vault::VideoContainer::Unknown));
}

TEST(detect_container_preserves_existing_mkv_detection)
{
    // MKV: EBML magic 0x1A45DFA3 at offset 0 (existing detection must be preserved)
    std::vector<uint8_t> buf(512, 0);
    buf[0] = 0x1A;
    buf[1] = 0x45;
    buf[2] = 0xDF;
    buf[3] = 0xA3;

    auto result = vault::detect_video_container(std::span(buf));
    CHECK_EQ(static_cast<int>(result), static_cast<int>(vault::VideoContainer::MKV));
}

TEST(detect_container_preserves_existing_mp4_detection)
{
    // ISO-BMFF/MP4: "ftyp" at offset 4 (existing detection must be preserved)
    std::vector<uint8_t> buf(512, 0);
    buf[4] = 'f';
    buf[5] = 't';
    buf[6] = 'y';
    buf[7] = 'p';

    auto result = vault::detect_video_container(std::span(buf));
    CHECK_EQ(static_cast<int>(result), static_cast<int>(vault::VideoContainer::MP4));
}
