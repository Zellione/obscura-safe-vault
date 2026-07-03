#include <libheif/heif.h>

#include <cstring>
#include <utility>

#include "image/decode.h"
#include "image/decoder.h"
#include "image/format_registry.h"

namespace image {

namespace {

// RAII for the libheif context — freed on every return path.
struct CtxGuard {
    heif_context* ctx;
    explicit CtxGuard(heif_context* c) noexcept : ctx(c) {}
    ~CtxGuard() { if (ctx) heif_context_free(ctx); }
    CtxGuard(const CtxGuard&) = delete;
    CtxGuard& operator=(const CtxGuard&) = delete;
};

} // namespace

// Decode a HEIC or AVIF buffer to 3-channel RGB. libheif dispatches to libde265
// (HEVC) or libaom (AV1) by container brand; both decoders are statically baked in
// (no plugin loading). Any libheif error yields nullopt rather than a crash, and
// all libheif objects are released before returning.
std::optional<ImageData> decode_heif_from_memory(std::span<const uint8_t> data)
{
    heif_context* ctx = heif_context_alloc();
    if (!ctx) return std::nullopt;
    CtxGuard guard(ctx);

    // Buffer must outlive the context (no copy); `data` does, for this call.
    if (heif_context_read_from_memory_without_copy(
            ctx, data.data(), data.size(), nullptr).code != heif_error_Ok)
        return std::nullopt;

    heif_image_handle* handle = nullptr;
    if (heif_context_get_primary_image_handle(ctx, &handle).code != heif_error_Ok || !handle)
        return std::nullopt;

    heif_image* img = nullptr;
    if (const heif_error err = heif_decode_image(
            handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr);
        err.code != heif_error_Ok || !img) {
        heif_image_handle_release(handle);
        return std::nullopt;
    }

    const int w = heif_image_get_width(img, heif_channel_interleaved);
    const int h = heif_image_get_height(img, heif_channel_interleaved);
    int stride  = 0;
    const uint8_t* plane =
        heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);

    std::optional<ImageData> out;
    if (plane && w > 0 && h > 0 && stride >= w * 3) {
        using enum ImageFormat;
        ImageData d;
        d.width  = w;
        d.height = h;
        // The container brand decides the tag; libheif handles either decoder.
        d.format = (detect_format(data) == AVIF) ? AVIF : HEIC;
        d.pixels.resize(static_cast<size_t>(w) * h * 3);
        // Copy row by row: the plane is padded to `stride`, our buffer is tight.
        for (int y = 0; y < h; ++y)
            std::memcpy(d.pixels.data() + static_cast<size_t>(y) * w * 3,
                        plane + static_cast<size_t>(y) * stride,
                        static_cast<size_t>(w) * 3);
        out = std::move(d);
    }

    heif_image_release(img);
    heif_image_handle_release(handle);
    return out;
}

namespace {

class HeifDecoder final : public Decoder {
public:
    [[nodiscard]] bool can_decode(std::span<const uint8_t> data) const noexcept override
    {
        const ImageFormat fmt = detect_format(data);
        return fmt == ImageFormat::HEIC || fmt == ImageFormat::AVIF;
    }
    [[nodiscard]] std::optional<ImageData> decode(std::span<const uint8_t> data) const override
    {
        return decode_heif_from_memory(data);
    }
};

} // namespace

std::unique_ptr<Decoder> make_heif_decoder() { return std::make_unique<HeifDecoder>(); }

} // namespace image
