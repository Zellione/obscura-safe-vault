#pragma once

#include <optional>
#include <span>

#include "image/image.h"

namespace image {

// Decode an in-memory image buffer into 3-channel RGB pixels. Dispatches by the
// container detected via format_registry: stb_image handles JPEG/PNG/GIF/BMP/TGA/HDR,
// libwebp handles WebP, libheif handles HEIC/AVIF. Always outputs 3-channel RGB
// regardless of the source's channel count (alpha is dropped; grayscale is expanded).
// Returns nullopt on corrupt data or unsupported format.
[[nodiscard]] std::optional<ImageData> decode_from_memory(std::span<const uint8_t> data);

// Codec-specific decoders used by the dispatcher (named per the ROADMAP). Each
// returns 3-channel RGB or nullopt on failure. Defined in decode_webp.cpp and
// decode_heif.cpp respectively.
[[nodiscard]] std::optional<ImageData> decode_webp_from_memory(std::span<const uint8_t> data);
[[nodiscard]] std::optional<ImageData> decode_heif_from_memory(std::span<const uint8_t> data);

} // namespace image
