#pragma once

// Magic-byte detection of the video container we accept (MP4/MOV, Matroska/WebM).
// Deliberately FFmpeg-free (Phase 15 PR2): just enough to validate an import and
// tag VideoMeta.container. Codec/dimension probing arrives with the decoder (PR4).

#include <cstdint>
#include <span>

#include "vault/index.h"   // for VideoContainer

namespace vault {

// Returns MP4 for an ISO-BMFF file-type box, MKV for an EBML (Matroska/WebM)
// header, Unknown otherwise (incl. short/empty input). Pure, never throws.
[[nodiscard]] VideoContainer detect_video_container(std::span<const uint8_t> data) noexcept;

} // namespace vault
