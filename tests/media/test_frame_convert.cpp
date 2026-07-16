#include "test_framework.h"

#ifdef OSV_VENDORED_AV

#include <cstring>
#include <vector>

#include "media/frame_convert.h"

TEST(copy_owned_frame_i420_preserves_pixel_data_and_survives_source_mutation)
{
    // Build a tiny synthetic I420 frame: 4x2 luma, 2x1 chroma planes.
    std::vector<uint8_t> y_plane{1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> u_plane{9, 10};
    std::vector<uint8_t> v_plane{11, 12};

    media::DecodedFrame src{};
    src.width       = 4;
    src.height      = 2;
    src.pix_fmt      = media::FramePixelFormat::I420;
    src.pts_seconds = 1.5;
    src.planes[0]    = y_plane.data();
    src.planes[1]    = u_plane.data();
    src.planes[2]    = v_plane.data();
    src.linesizes[0] = 4;
    src.linesizes[1] = 2;
    src.linesizes[2] = 2;

    std::vector<uint8_t> storage;
    media::DecodedFrame owned = media::copy_owned_frame(src, storage);

    CHECK(owned.width == 4);
    CHECK(owned.height == 2);
    CHECK(owned.pix_fmt == media::FramePixelFormat::I420);
    CHECK(owned.pts_seconds == 1.5);
    REQUIRE(owned.planes[0] != nullptr);
    REQUIRE(owned.planes[1] != nullptr);
    REQUIRE(owned.planes[2] != nullptr);
    CHECK(std::memcmp(owned.planes[0], y_plane.data(), y_plane.size()) == 0);
    CHECK(std::memcmp(owned.planes[1], u_plane.data(), u_plane.size()) == 0);
    CHECK(std::memcmp(owned.planes[2], v_plane.data(), v_plane.size()) == 0);

    // Mutate the source; the owned copy must be unaffected (proves it's a
    // real copy, not an alias — the property the cross-thread handoff needs).
    y_plane[0] = 0xFF;
    CHECK(std::memcmp(owned.planes[0], "\x01\x02\x03\x04", 4) == 0);
}

TEST(copy_owned_frame_nv12_has_two_planes_third_null)
{
    std::vector<uint8_t> y_plane{1, 2, 3, 4};
    std::vector<uint8_t> uv_plane{5, 6};

    media::DecodedFrame src{};
    src.width        = 2;
    src.height       = 2;
    src.pix_fmt       = media::FramePixelFormat::NV12;
    src.pts_seconds  = 0.0;
    src.planes[0]     = y_plane.data();
    src.planes[1]     = uv_plane.data();
    src.planes[2]     = nullptr;
    src.linesizes[0]  = 2;
    src.linesizes[1]  = 2;
    src.linesizes[2]  = 0;

    std::vector<uint8_t> storage;
    media::DecodedFrame owned = media::copy_owned_frame(src, storage);

    CHECK(owned.pix_fmt == media::FramePixelFormat::NV12);
    REQUIRE(owned.planes[0] != nullptr);
    REQUIRE(owned.planes[1] != nullptr);
    CHECK(owned.planes[2] == nullptr);
    CHECK(std::memcmp(owned.planes[0], y_plane.data(), 4) == 0);
    CHECK(std::memcmp(owned.planes[1], uv_plane.data(), 2) == 0);
}

#endif // OSV_VENDORED_AV
