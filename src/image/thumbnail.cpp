#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#pragma GCC diagnostic pop

#include "image/thumbnail.h"

#include <algorithm>
#include <cstdint>

namespace image {

std::optional<std::vector<uint8_t>>
make_thumbnail(const ImageData& src, int max_side, int quality)
{
    if (src.pixels.empty() || src.width <= 0 || src.height <= 0 || max_side <= 0)
        return std::nullopt;

    // Fit within max_side preserving aspect ratio; never upscale.
    int tw = 0;
    int th = 0;
    if (src.width >= src.height) {
        tw = std::min(src.width, max_side);
        th = std::max(1, static_cast<int>(static_cast<int64_t>(src.height) * tw / src.width));
    } else {
        th = std::min(src.height, max_side);
        tw = std::max(1, static_cast<int>(static_cast<int64_t>(src.width) * th / src.height));
    }

    std::vector<uint8_t> resized(static_cast<size_t>(tw) * th * 3);
    // STBIR_RGB == 3; cast documented as valid for back-compat with old channel-count API.
    if (!stbir_resize_uint8_linear(
            src.pixels.data(), src.width, src.height, 0,
            resized.data(), tw, th, 0,
            static_cast<stbir_pixel_layout>(3)))
        return std::nullopt;

    std::vector<uint8_t> jpeg;
    // NOSONAR cpp:S5008 — void* is mandated by the stbi_write_func C callback signature.
    auto write_fn = [](void* ctx, void* data, int size) { // NOSONAR cpp:S5008
        auto& out = *static_cast<std::vector<uint8_t>*>(ctx);
        const auto* p = static_cast<const uint8_t*>(data);
        out.insert(out.end(), p, p + size);
    };
    if (!stbi_write_jpg_to_func(write_fn, &jpeg, tw, th, 3, resized.data(), quality) ||
        jpeg.empty())
        return std::nullopt;

    return jpeg;
}

} // namespace image
