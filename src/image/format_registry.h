#pragma once

#include <cstdint>
#include <span>

#include "image/image.h"

namespace image {

// Identify an image container from its leading magic bytes. Reads only the bytes
// it inspects (never past the span's end) and never decodes. Returns Unknown when
// no magic matches — callers may still attempt a last-resort decoder (e.g. stb's
// TGA path, which has no reliable signature). The returned value mirrors
// vault::ImageFormat so a static_cast between the two stays valid.
[[nodiscard]] ImageFormat detect_format(std::span<const uint8_t> data) noexcept;

} // namespace image
