#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "vault/index.h"

namespace media {

// Result of probing a video file.
struct VideoProbeResult {
    vault::VideoContainer container = vault::VideoContainer::Unknown;
    vault::VideoCodec codec         = vault::VideoCodec::Unknown;
    uint32_t width                  = 0;
    uint32_t height                 = 0;
    uint64_t duration_us            = 0;
    std::vector<uint8_t> poster_jpeg;  // empty if poster generation failed or FFmpeg unavailable
};

// Phase 65: decode-capability generation. Bump whenever this build can decode a
// container/codec it previously could not (a new FFmpeg decoder enabled, a new
// vendored codec library). A vault whose migrated_probe_caps is below this has
// videos worth re-probing; one at or above it does not, so an undecodable video
// is not re-read on every unlock forever.
inline constexpr uint16_t PROBE_CAPS_GEN = 1;

// Probe a video file (plaintext in-memory data). Returns true if the container is
// recognized; fills `out` with detected metadata.
// - With FFmpeg: opens via MemAvio, decodes first frame as RGB, scales to JPEG poster.
// - Without FFmpeg: falls back to magic-byte detection; returns false if container is Unknown.
[[nodiscard]] bool probe_video(std::span<const uint8_t> data, VideoProbeResult& out);

} // namespace media
