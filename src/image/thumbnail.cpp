#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "image/thumbnail.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>

namespace image {

std::optional<crypto::SecureBytes>
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

    // Phase 96 (OSV-AUD-003): resized RGB pixels are plaintext derived content —
    // mlock'd, wipe-on-release SecureBytes, never std::vector<uint8_t>.
    crypto::SecureBytes resized;
    const size_t resized_bytes = static_cast<size_t>(tw) * static_cast<size_t>(th) * 3;
    if (!resized.resize(resized_bytes)) return std::nullopt;
    // STBIR_RGB == 3; cast documented as valid for back-compat with old channel-count API.
    if (!stbir_resize_uint8_linear(
            src.pixels.data(), src.width, src.height, 0,
            resized.data(), tw, th, 0,
            static_cast<stbir_pixel_layout>(3)))
        return std::nullopt;

    crypto::SecureBytes jpeg;
    // NOSONAR cpp:S5008 — void* is mandated by the stbi_write_func C callback
    // signature. The callback is capture-less (stbi writes want a function
    // pointer), so the growable secure sink rides in `ctx`. `failed` turns a
    // mid-encode allocation failure into a failed thumbnail (never a silently
    // truncated JPEG): stbi's void callback can't report OOM.
    struct JpegSink {
        crypto::SecureBytes bytes;
        bool                failed = false;
    };
    JpegSink sink;
    auto write_fn = [](void* ctx, void* data, int size) { // NOSONAR cpp:S5008
        auto& out     = *static_cast<JpegSink*>(ctx);
        const auto* p = static_cast<const uint8_t*>(data);
        if (!out.bytes.append(std::span(p, static_cast<size_t>(size)))) out.failed = true;
    };
    if (!stbi_write_jpg_to_func(write_fn, &sink, tw, th, 3, resized.data(), quality) ||
        sink.failed || sink.bytes.empty())
        return std::nullopt;

    return std::move(sink.bytes);
}

} // namespace image