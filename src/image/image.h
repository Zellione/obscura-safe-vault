#pragma once

#include <cstdint>

#include "crypto/secure_mem.h"

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
// Pixels are plaintext image data, so they live in mlock'd SecureBytes
// (invariant #1, best-effort page-lock + crypto_wipe on destruction) exactly
// like the encrypted stored bytes — never in swappable memory unmarked.
struct ImageData {
    crypto::SecureBytes pixels;
    int         width  = 0;
    int         height = 0;
    ImageFormat format = ImageFormat::Unknown;
};

} // namespace image
