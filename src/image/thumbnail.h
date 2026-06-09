#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "image/image.h"

namespace image {

// Downscale `src` so neither dimension exceeds `max_side` (no upscaling if already
// smaller), encode the result as JPEG at `quality` (0-100), and return the bytes.
// Returns nullopt if `src` is empty or resize/encode fails.
[[nodiscard]] std::optional<std::vector<uint8_t>>
make_thumbnail(const ImageData& src, int max_side, int quality = 85);

} // namespace image
