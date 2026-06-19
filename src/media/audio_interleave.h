#pragma once

#include <cstdint>
#include <vector>

#ifdef OSV_VENDORED_AV

extern "C" {
#include <libavutil/samplefmt.h>
}

namespace media {

// Pure, testable audio sample format interleaving.
// Takes raw plane pointers + AVSampleFormat, fills interleaved F32 output.
// No FFmpeg context needed; can be tested with synthetic data.
//
// @param planes: array of pointers to each plane's samples (size: n_planes)
// @param n_planes: 1 for packed formats, ch for planar formats
// @param nb_samples: number of samples per plane
// @param channels: number of audio channels
// @param av_sample_fmt: AVSampleFormat enum value (AV_SAMPLE_FMT_* int)
// @param out: output vector, resized to nb_samples * channels
[[nodiscard]] bool interleave_to_f32(const uint8_t* const* planes, int n_planes,
                                      int nb_samples, int channels,
                                      int av_sample_fmt,
                                      std::vector<float>& out) noexcept;

}  // namespace media

#endif  // OSV_VENDORED_AV
