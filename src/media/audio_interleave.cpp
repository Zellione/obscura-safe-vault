#ifdef OSV_VENDORED_AV

#include "media/audio_interleave.h"

#include <cstring>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
extern "C" {
#include <libavutil/samplefmt.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace media {

namespace {

// Helper: read a sample from a byte plane at index i, cast to float via T.
template <typename T>
inline float read_sample_as_float(const uint8_t* plane, int index);

// Specializations for each format's normalization.
template <>
inline float read_sample_as_float<float>(const uint8_t* plane, int index)
{
    float sample = 0.0f;
    std::memcpy(&sample, plane + index * static_cast<int>(sizeof(float)),
                sizeof(float));
    return sample;
}

template <>
inline float read_sample_as_float<int16_t>(const uint8_t* plane, int index)
{
    int16_t sample = 0;
    std::memcpy(&sample, plane + index * static_cast<int>(sizeof(int16_t)),
                sizeof(int16_t));
    return sample / 32768.0f;
}

template <>
inline float read_sample_as_float<int32_t>(const uint8_t* plane, int index)
{
    int32_t sample = 0;
    std::memcpy(&sample, plane + index * static_cast<int>(sizeof(int32_t)),
                sizeof(int32_t));
    return static_cast<float>(sample) / 2147483648.0f;
}

template <>
inline float read_sample_as_float<uint8_t>(const uint8_t* plane, int index)
{
    uint8_t sample = 0;
    std::memcpy(&sample, plane + index, sizeof(uint8_t));
    return (static_cast<float>(sample) - 128.0f) / 128.0f;
}

// Interleave planar samples (one plane per channel) to F32.
template <typename T>
inline void interleave_planar(const uint8_t* const* planes, int nb_samples,
                              int channels, std::vector<float>& out) noexcept
{
    out.resize(nb_samples * channels);
    for (int s = 0; s < nb_samples; ++s) {
        for (int c = 0; c < channels; ++c) {
            out[s * channels + c] =
                read_sample_as_float<T>(planes[c], s);
        }
    }
}

// Interleave packed samples (all channels in one plane) to F32.
template <typename T>
inline void interleave_packed(const uint8_t* const* planes, int nb_samples,
                              int channels, std::vector<float>& out) noexcept
{
    out.resize(nb_samples * channels);
    const uint8_t* src = planes[0];
    for (int i = 0; i < nb_samples * channels; ++i) {
        out[i] = read_sample_as_float<T>(src, i);
    }
}

}  // namespace

bool interleave_to_f32(const uint8_t* const* planes, int n_planes,
                        int nb_samples, int channels, int av_sample_fmt,
                        std::vector<float>& out) noexcept
{
    // Input validation: avoid crashes on bad parameters
    if (!planes || n_planes <= 0 || nb_samples < 0 || channels <= 0) {
        return false;
    }
    if (planes[0] == nullptr) {
        return false;
    }

    auto fmt = static_cast<AVSampleFormat>(av_sample_fmt);

    switch (fmt) {
        case AV_SAMPLE_FMT_FLTP:
            // Planar float32
            interleave_planar<float>(planes, nb_samples, channels, out);
            return true;

        case AV_SAMPLE_FMT_FLT:
            // Packed float32
            interleave_packed<float>(planes, nb_samples, channels, out);
            return true;

        case AV_SAMPLE_FMT_S16P:
            // Planar int16
            interleave_planar<int16_t>(planes, nb_samples, channels, out);
            return true;

        case AV_SAMPLE_FMT_S16:
            // Packed int16
            interleave_packed<int16_t>(planes, nb_samples, channels, out);
            return true;

        case AV_SAMPLE_FMT_S32P:
            // Planar int32
            interleave_planar<int32_t>(planes, nb_samples, channels, out);
            return true;

        case AV_SAMPLE_FMT_S32:
            // Packed int32
            interleave_packed<int32_t>(planes, nb_samples, channels, out);
            return true;

        case AV_SAMPLE_FMT_U8P:
            // Planar uint8
            interleave_planar<uint8_t>(planes, nb_samples, channels, out);
            return true;

        case AV_SAMPLE_FMT_U8:
            // Packed uint8
            interleave_packed<uint8_t>(planes, nb_samples, channels, out);
            return true;

        default:
            // Unsupported format
            return false;
    }
}

}  // namespace media

#endif  // OSV_VENDORED_AV
