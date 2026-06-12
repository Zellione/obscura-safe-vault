#pragma once

#include <cstdint>
#include <vector>

namespace image {

// Format tag detected from magic bytes. Values deliberately match vault::ImageFormat
// so that a static_cast between the two enums is always valid.
enum class ImageFormat : uint8_t {
    JPEG    = 0,
    PNG     = 1,
    GIF     = 2,
    BMP     = 3,
    TGA     = 4,
    HDR     = 5,
    WebP    = 6,
    HEIC    = 7,
    AVIF    = 8,
    Unknown = 0xFF,
};

// Decoded image: always 3-channel RGB, width*height*3 bytes, row-major.
// std::vector is sufficient here; decoded pixels are transient (never written to
// disk), so mlock is unnecessary for this intermediate buffer.
struct ImageData {
    std::vector<uint8_t> pixels;
    int         width  = 0;
    int         height = 0;
    ImageFormat format = ImageFormat::Unknown;
};

} // namespace image
