#pragma once

#include <optional>
#include <span>

#include "image/image.h"

namespace image {

// Decode an in-memory image buffer (JPEG, PNG, GIF, BMP, TGA, HDR) into RGB pixels.
// Always outputs 3-channel RGB regardless of the source's channel count (alpha is
// dropped; grayscale is expanded). Returns nullopt on corrupt data or unsupported format.
[[nodiscard]] std::optional<ImageData> decode_from_memory(std::span<const uint8_t> data);

} // namespace image
