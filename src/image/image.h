#pragma once

// Phase 3 stub: decode images from decrypted memory buffers + generate thumbnails.
// Full implementation in Phase 3.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace image {

// TODO (Phase 3): ImageFormat enum (JPEG, PNG, GIF, BMP, TGA, HDR, WebP, HEIC)
// TODO (Phase 3): ImageData   { uint8_t* pixels, width, height, channels }
//                              decoded via stb_image directly from encrypted buffer
// TODO (Phase 3): decode_from_memory(std::span<const uint8_t> encrypted_buf) -> ImageData
// TODO (Phase 3): make_thumbnail(ImageData, int max_side) -> ImageData
//                 for pre-generating thumb entries stored encrypted in the vault

} // namespace image
