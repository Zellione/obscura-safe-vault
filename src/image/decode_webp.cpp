#include <webp/decode.h>

#include "image/decode.h"

namespace image {

// Decode a WebP buffer to 3-channel RGB. libwebp validates the container and
// rejects truncated/corrupt input via WebPGetInfo / WebPDecodeRGBInto, so a bad
// buffer yields nullopt rather than a crash.
std::optional<ImageData> decode_webp_from_memory(std::span<const uint8_t> data)
{
    int w = 0;
    int h = 0;
    if (!WebPGetInfo(data.data(), data.size(), &w, &h) || w <= 0 || h <= 0)
        return std::nullopt;

    ImageData img;
    img.width  = w;
    img.height = h;
    img.format = ImageFormat::WebP;
    img.pixels.resize(static_cast<size_t>(w) * h * 3);

    const int stride = w * 3;
    if (!WebPDecodeRGBInto(data.data(), data.size(),
                           img.pixels.data(), img.pixels.size(), stride))
        return std::nullopt;

    return img;
}

} // namespace image
