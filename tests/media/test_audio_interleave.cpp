#include "../test_framework.h"

#ifdef OSV_VENDORED_AV

#include "media/audio_interleave.h"

#include <cstring>
#include <vector>

extern "C" {
#include <libavutil/samplefmt.h>
}

namespace {

// Helper to build a planar test buffer: planes is an array of pointers,
// each plane contains nb_samples of the given type T.
template <typename T>
std::vector<std::vector<uint8_t>> make_planar_planes(int ch, int nb_samples,
                                                      const T* samples)
{
    std::vector<std::vector<uint8_t>> planes(ch);
    int idx = 0;
    for (int c = 0; c < ch; ++c) {
        planes[c].resize(nb_samples * sizeof(T));
        for (int s = 0; s < nb_samples; ++s) {
            std::memcpy(planes[c].data() + s * sizeof(T), &samples[idx],
                        sizeof(T));
            ++idx;
        }
    }
    return planes;
}

// Helper to build a packed test buffer: single plane with all samples.
template <typename T>
std::vector<std::vector<uint8_t>> make_packed_plane(int ch, int nb_samples,
                                                     const T* samples)
{
    std::vector<std::vector<uint8_t>> planes(1);
    planes[0].resize(nb_samples * ch * sizeof(T));
    std::memcpy(planes[0].data(), samples,
                nb_samples * ch * sizeof(T));
    return planes;
}

// Helper to build pointer array from planes.
std::vector<const uint8_t*> get_plane_ptrs(
    const std::vector<std::vector<uint8_t>>& planes)
{
    std::vector<const uint8_t*> ptrs(planes.size());
    for (size_t i = 0; i < planes.size(); ++i) {
        ptrs[i] = planes[i].data();
    }
    return ptrs;
}

}  // namespace

// ============================================================================
// Planar FLTP (float) tests
// ============================================================================

TEST(interleave_fltp_mono_basic)
{
    const float samples[] = {0.1f, 0.2f, 0.3f};
    auto planes = make_planar_planes(1, 3, samples);
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(media::interleave_to_f32(ptrs.data(), 1, 3, 1, AV_SAMPLE_FMT_FLTP, out));
    REQUIRE(out.size() == 3);
    REQUIRE(out[0] == 0.1f);
    REQUIRE(out[1] == 0.2f);
    REQUIRE(out[2] == 0.3f);
}

TEST(interleave_fltp_stereo_basic)
{
    // Plane 0: [0.1, 0.3], Plane 1: [0.2, 0.4] -> interleaved [0.1, 0.2, 0.3, 0.4]
    const float left[] = {0.1f, 0.3f};
    const float right[] = {0.2f, 0.4f};
    std::vector<std::vector<uint8_t>> planes(2);
    planes[0].resize(2 * sizeof(float));
    planes[1].resize(2 * sizeof(float));
    std::memcpy(planes[0].data(), left, 2 * sizeof(float));
    std::memcpy(planes[1].data(), right, 2 * sizeof(float));
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(media::interleave_to_f32(ptrs.data(), 2, 2, 2, AV_SAMPLE_FMT_FLTP, out));
    REQUIRE(out.size() == 4);
    REQUIRE(out[0] == 0.1f);
    REQUIRE(out[1] == 0.2f);
    REQUIRE(out[2] == 0.3f);
    REQUIRE(out[3] == 0.4f);
}

// ============================================================================
// Packed FLT (float) tests
// ============================================================================

TEST(interleave_flt_mono_basic)
{
    const float samples[] = {0.1f, 0.2f, 0.3f};
    auto planes = make_packed_plane(1, 3, samples);
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(media::interleave_to_f32(ptrs.data(), 1, 3, 1, AV_SAMPLE_FMT_FLT, out));
    REQUIRE(out.size() == 3);
    REQUIRE(out[0] == 0.1f);
    REQUIRE(out[1] == 0.2f);
    REQUIRE(out[2] == 0.3f);
}

TEST(interleave_flt_stereo_basic)
{
    // Packed: [L0, R0, L1, R1]
    const float samples[] = {0.1f, 0.2f, 0.3f, 0.4f};
    auto planes = make_packed_plane(2, 2, samples);
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(media::interleave_to_f32(ptrs.data(), 1, 2, 2, AV_SAMPLE_FMT_FLT, out));
    REQUIRE(out.size() == 4);
    REQUIRE(out[0] == 0.1f);
    REQUIRE(out[1] == 0.2f);
    REQUIRE(out[2] == 0.3f);
    REQUIRE(out[3] == 0.4f);
}

// ============================================================================
// Planar S16P (int16) tests
// ============================================================================

TEST(interleave_s16p_mono_basic)
{
    const int16_t samples[] = {16384, 32767, -32768};
    auto planes = make_planar_planes(1, 3, samples);
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(media::interleave_to_f32(ptrs.data(), 1, 3, 1, AV_SAMPLE_FMT_S16P, out));
    REQUIRE(out.size() == 3);
    // 16384 / 32768 = 0.5
    REQUIRE(out[0] > 0.4999f && out[0] < 0.5001f);
    // 32767 / 32768 ≈ 0.99997
    REQUIRE(out[1] > 0.9999f && out[1] < 1.0f);
    // -32768 / 32768 = -1.0
    REQUIRE(out[2] == -1.0f);
}

TEST(interleave_s16p_stereo_basic)
{
    const int16_t left[] = {16384, -16384};
    const int16_t right[] = {8192, -8192};
    std::vector<std::vector<uint8_t>> planes(2);
    planes[0].resize(2 * sizeof(int16_t));
    planes[1].resize(2 * sizeof(int16_t));
    std::memcpy(planes[0].data(), left, 2 * sizeof(int16_t));
    std::memcpy(planes[1].data(), right, 2 * sizeof(int16_t));
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(media::interleave_to_f32(ptrs.data(), 2, 2, 2, AV_SAMPLE_FMT_S16P, out));
    REQUIRE(out.size() == 4);
    REQUIRE(out[0] > 0.4999f && out[0] < 0.5001f);    // 16384/32768
    REQUIRE(out[1] > 0.2499f && out[1] < 0.2501f);    // 8192/32768
    REQUIRE(out[2] > -0.5001f && out[2] < -0.4999f);  // -16384/32768
    REQUIRE(out[3] > -0.2501f && out[3] < -0.2499f);  // -8192/32768
}

// ============================================================================
// Packed S16 (int16) tests
// ============================================================================

TEST(interleave_s16_mono_basic)
{
    const int16_t samples[] = {16384, 32767, -32768};
    auto planes = make_packed_plane(1, 3, samples);
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(media::interleave_to_f32(ptrs.data(), 1, 3, 1, AV_SAMPLE_FMT_S16, out));
    REQUIRE(out.size() == 3);
    REQUIRE(out[0] > 0.4999f && out[0] < 0.5001f);
    REQUIRE(out[1] > 0.9999f && out[1] < 1.0f);
    REQUIRE(out[2] == -1.0f);
}

TEST(interleave_s16_stereo_basic)
{
    // Packed: [L0, R0, L1, R1]
    const int16_t samples[] = {16384, 8192, -16384, -8192};
    auto planes = make_packed_plane(2, 2, samples);
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(media::interleave_to_f32(ptrs.data(), 1, 2, 2, AV_SAMPLE_FMT_S16, out));
    REQUIRE(out.size() == 4);
    REQUIRE(out[0] > 0.4999f && out[0] < 0.5001f);
    REQUIRE(out[1] > 0.2499f && out[1] < 0.2501f);
    REQUIRE(out[2] > -0.5001f && out[2] < -0.4999f);
    REQUIRE(out[3] > -0.2501f && out[3] < -0.2499f);
}

// ============================================================================
// Planar S32P (int32) tests
// ============================================================================

TEST(interleave_s32p_mono_basic)
{
    const int32_t samples[] = {1073741824, 2147483647, -2147483648};
    auto planes = make_planar_planes(1, 3, samples);
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(media::interleave_to_f32(ptrs.data(), 1, 3, 1, AV_SAMPLE_FMT_S32P, out));
    REQUIRE(out.size() == 3);
    // 1073741824 / 2147483648 = 0.5
    REQUIRE(out[0] > 0.4999f && out[0] < 0.5001f);
    // Large int32 / 2147483648 ≈ 0.99999999f
    REQUIRE(out[1] > 0.9999f && out[1] <= 1.0f);
    // -2147483648 / 2147483648 = -1.0
    REQUIRE(out[2] <= -0.9999f && out[2] >= -1.0f);
}

TEST(interleave_s32p_stereo_basic)
{
    const int32_t left[] = {1073741824, -1073741824};
    const int32_t right[] = {536870912, -536870912};
    std::vector<std::vector<uint8_t>> planes(2);
    planes[0].resize(2 * sizeof(int32_t));
    planes[1].resize(2 * sizeof(int32_t));
    std::memcpy(planes[0].data(), left, 2 * sizeof(int32_t));
    std::memcpy(planes[1].data(), right, 2 * sizeof(int32_t));
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(media::interleave_to_f32(ptrs.data(), 2, 2, 2, AV_SAMPLE_FMT_S32P, out));
    REQUIRE(out.size() == 4);
    REQUIRE(out[0] > 0.4999f && out[0] < 0.5001f);    // 1073741824/2147483648
    REQUIRE(out[1] > 0.2499f && out[1] < 0.2501f);    // 536870912/2147483648
    REQUIRE(out[2] > -0.5001f && out[2] < -0.4999f);  // -1073741824/2147483648
    REQUIRE(out[3] > -0.2501f && out[3] < -0.2499f);  // -536870912/2147483648
}

// ============================================================================
// Packed S32 (int32) tests
// ============================================================================

TEST(interleave_s32_mono_basic)
{
    const int32_t samples[] = {1073741824, 2147483647, -2147483648};
    auto planes = make_packed_plane(1, 3, samples);
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(media::interleave_to_f32(ptrs.data(), 1, 3, 1, AV_SAMPLE_FMT_S32, out));
    REQUIRE(out.size() == 3);
    REQUIRE(out[0] > 0.4999f && out[0] < 0.5001f);
    REQUIRE(out[1] > 0.9999f && out[1] <= 1.0f);
    REQUIRE(out[2] <= -0.9999f && out[2] >= -1.0f);
}

TEST(interleave_s32_stereo_basic)
{
    const int32_t samples[] = {1073741824, 536870912, -1073741824, -536870912};
    auto planes = make_packed_plane(2, 2, samples);
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(media::interleave_to_f32(ptrs.data(), 1, 2, 2, AV_SAMPLE_FMT_S32, out));
    REQUIRE(out.size() == 4);
    REQUIRE(out[0] > 0.4999f && out[0] < 0.5001f);
    REQUIRE(out[1] > 0.2499f && out[1] < 0.2501f);
    REQUIRE(out[2] > -0.5001f && out[2] < -0.4999f);
    REQUIRE(out[3] > -0.2501f && out[3] < -0.2499f);
}

// ============================================================================
// Planar U8P (uint8) tests
// ============================================================================

TEST(interleave_u8p_mono_basic)
{
    const uint8_t samples[] = {128, 192, 64};  // 128 is zero, 192 is +1, 64 is -0.5
    auto planes = make_planar_planes(1, 3, samples);
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(media::interleave_to_f32(ptrs.data(), 1, 3, 1, AV_SAMPLE_FMT_U8P, out));
    REQUIRE(out.size() == 3);
    REQUIRE(out[0] > -0.0001f && out[0] < 0.0001f);    // (128-128)/128 = 0
    REQUIRE(out[1] > 0.4999f && out[1] < 0.5001f);    // (192-128)/128 = 0.5
    REQUIRE(out[2] > -0.5001f && out[2] < -0.4999f);  // (64-128)/128 = -0.5
}

TEST(interleave_u8p_stereo_basic)
{
    const uint8_t left[] = {128, 192};
    const uint8_t right[] = {192, 64};
    std::vector<std::vector<uint8_t>> planes(2);
    planes[0].resize(2);
    planes[1].resize(2);
    std::memcpy(planes[0].data(), left, 2);
    std::memcpy(planes[1].data(), right, 2);
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(media::interleave_to_f32(ptrs.data(), 2, 2, 2, AV_SAMPLE_FMT_U8P, out));
    REQUIRE(out.size() == 4);
    REQUIRE(out[0] > -0.0001f && out[0] < 0.0001f);    // (128-128)/128
    REQUIRE(out[1] > 0.4999f && out[1] < 0.5001f);    // (192-128)/128
    REQUIRE(out[2] > 0.4999f && out[2] < 0.5001f);    // (192-128)/128
    REQUIRE(out[3] > -0.5001f && out[3] < -0.4999f);  // (64-128)/128
}

// ============================================================================
// Packed U8 (uint8) tests
// ============================================================================

TEST(interleave_u8_mono_basic)
{
    const uint8_t samples[] = {128, 192, 64};
    auto planes = make_packed_plane(1, 3, samples);
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(media::interleave_to_f32(ptrs.data(), 1, 3, 1, AV_SAMPLE_FMT_U8, out));
    REQUIRE(out.size() == 3);
    REQUIRE(out[0] > -0.0001f && out[0] < 0.0001f);
    REQUIRE(out[1] > 0.4999f && out[1] < 0.5001f);
    REQUIRE(out[2] > -0.5001f && out[2] < -0.4999f);
}

TEST(interleave_u8_stereo_basic)
{
    // Packed: [L0, R0, L1, R1]
    const uint8_t samples[] = {128, 192, 192, 64};
    auto planes = make_packed_plane(2, 2, samples);
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(media::interleave_to_f32(ptrs.data(), 1, 2, 2, AV_SAMPLE_FMT_U8, out));
    REQUIRE(out.size() == 4);
    REQUIRE(out[0] > -0.0001f && out[0] < 0.0001f);
    REQUIRE(out[1] > 0.4999f && out[1] < 0.5001f);
    REQUIRE(out[2] > 0.4999f && out[2] < 0.5001f);
    REQUIRE(out[3] > -0.5001f && out[3] < -0.4999f);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(interleave_rejects_null_planes)
{
    std::vector<float> out;
    REQUIRE(!media::interleave_to_f32(nullptr, 1, 3, 1, AV_SAMPLE_FMT_FLTP, out));
}

TEST(interleave_rejects_null_plane_zero)
{
    const uint8_t* ptrs[] = {nullptr};
    std::vector<float> out;
    REQUIRE(!media::interleave_to_f32(ptrs, 1, 3, 1, AV_SAMPLE_FMT_FLTP, out));
}

TEST(interleave_rejects_negative_samples)
{
    const float samples[] = {0.1f};
    auto planes = make_planar_planes(1, 1, samples);
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(!media::interleave_to_f32(ptrs.data(), 1, -1, 1, AV_SAMPLE_FMT_FLTP, out));
}

TEST(interleave_rejects_zero_channels)
{
    const float samples[] = {0.1f};
    auto planes = make_planar_planes(1, 1, samples);
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    REQUIRE(!media::interleave_to_f32(ptrs.data(), 1, 1, 0, AV_SAMPLE_FMT_FLTP, out));
}

TEST(interleave_rejects_unsupported_format)
{
    const float samples[] = {0.1f};
    auto planes = make_planar_planes(1, 1, samples);
    auto ptrs = get_plane_ptrs(planes);

    std::vector<float> out;
    // Use an invalid format code
    REQUIRE(!media::interleave_to_f32(ptrs.data(), 1, 1, 1, -999, out));
}

#endif  // OSV_VENDORED_AV
