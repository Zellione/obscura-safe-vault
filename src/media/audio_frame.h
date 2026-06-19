#pragma once

#include <vector>

namespace media {

// Decoded PCM, interleaved float32. Transient: consumed into the audio device
// stream and dropped. Never written to disk (same contract as DecodedFrame).
struct AudioFrame {
    std::vector<float> samples;       // length = frame_count * channels
    int    channels    = 0;
    int    sample_rate = 0;
    double pts_seconds = 0.0;
};

}  // namespace media
