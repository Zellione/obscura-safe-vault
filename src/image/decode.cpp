#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <stb_image.h>

#include "image/decode.h"

namespace image {

namespace {

ImageFormat detect_format(std::span<const uint8_t> d) noexcept
{
    using enum ImageFormat;
    // Magic-byte detection; values must stay in sync with the ImageFormat enum.
    if (d.size() >= 2 && d[0] == 0xFF && d[1] == 0xD8)                               return JPEG;
    if (d.size() >= 4 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return PNG;
    if (d.size() >= 3 && d[0] == 'G'  && d[1] == 'I' && d[2] == 'F')                return GIF;
    if (d.size() >= 2 && d[0] == 'B'  && d[1] == 'M')                                return BMP;
    if (d.size() >= 2 && d[0] == '#'  && d[1] == '?')                                return HDR;
    // TGA has no reliable magic bytes; treat as last resort.
    return TGA;
}

} // namespace

std::optional<ImageData> decode_from_memory(std::span<const uint8_t> data)
{
    if (data.empty()) return std::nullopt;

    int w      = 0;
    int h      = 0;
    int src_ch = 0;
    // Force 3-channel RGB: simplifies thumbnail generation and GPU upload.
    stbi_uc* raw = stbi_load_from_memory(
        data.data(), static_cast<int>(data.size()), &w, &h, &src_ch, 3);
    if (!raw) return std::nullopt;

    ImageData img;
    img.width  = w;
    img.height = h;
    img.format = detect_format(data);
    img.pixels.assign(raw, raw + static_cast<size_t>(w) * h * 3);
    stbi_image_free(raw);
    return img;
}

} // namespace image
