#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <stb_image.h>

#include "image/decode.h"
#include "image/format_registry.h"

namespace image {

namespace {

// stb_image path for JPEG/PNG/GIF/BMP/TGA/HDR. `fmt` is the registry's verdict;
// TGA has no magic (registry returns Unknown), so Unknown is tagged as TGA — stb
// is the last-resort decoder for headerless raster data.
std::optional<ImageData> decode_stb(std::span<const uint8_t> data, ImageFormat fmt)
{
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
    img.format = (fmt == ImageFormat::Unknown) ? ImageFormat::TGA : fmt;
    img.pixels.assign(raw, raw + static_cast<size_t>(w) * h * 3);
    stbi_image_free(raw);
    return img;
}

} // namespace

std::optional<ImageData> decode_from_memory(std::span<const uint8_t> data)
{
    if (data.empty()) return std::nullopt;

    const ImageFormat fmt = detect_format(data);
    switch (fmt) {
        case ImageFormat::WebP: return decode_webp_from_memory(data);
        case ImageFormat::HEIC:
        case ImageFormat::AVIF: return decode_heif_from_memory(data);
        default:                return decode_stb(data, fmt);
    }
}

// TEMP (Stage A scaffolding): the real decode_heif_from_memory lands in
// decode_heif.cpp (Task B5), which removes this stub. decode_webp_from_memory is
// implemented in decode_webp.cpp.
std::optional<ImageData> decode_heif_from_memory(std::span<const uint8_t>) { return std::nullopt; }

} // namespace image
