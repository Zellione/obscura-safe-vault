#include <webp/decode.h>
#include <webp/demux.h>

#include "image/anim_info.h"
#include "image/decode.h"
#include "image/decoder.h"
#include "image/format_registry.h"

namespace image {

namespace {

// Decode frame 0 of an animated WebP to 3-channel RGB, flattening alpha over
// opaque black. The app carries no transparency anywhere — ImageData is RGB and
// thumbnails are JPEG — so this matches how transparent PNGs and GIFs already
// display. Returns nullopt if libwebp cannot open the animation.
std::optional<ImageData> decode_animated_webp_first_frame(std::span<const uint8_t> data)
{
    const WebPData webp_data{.bytes = data.data(), .size = data.size()};

    WebPAnimDecoderOptions opts;
    if (WebPAnimDecoderOptionsInit(&opts) == 0) {
        return std::nullopt;
    }
    opts.color_mode  = MODE_RGBA;
    opts.use_threads = 0;

    WebPAnimDecoder* dec = WebPAnimDecoderNew(&webp_data, &opts);
    if (dec == nullptr) {
        return std::nullopt;
    }

    WebPAnimInfo info{};
    uint8_t*     rgba         = nullptr;
    int          timestamp_ms = 0;
    std::optional<ImageData> out;

    if (WebPAnimDecoderGetInfo(dec, &info) != 0 && info.canvas_width > 0
        && info.canvas_height > 0 && WebPAnimDecoderHasMoreFrames(dec) != 0
        && WebPAnimDecoderGetNext(dec, &rgba, &timestamp_ms) != 0 && rgba != nullptr) {
        const auto w = static_cast<size_t>(info.canvas_width);
        const auto h = static_cast<size_t>(info.canvas_height);

        ImageData img;
        img.width  = static_cast<int>(info.canvas_width);
        img.height = static_cast<int>(info.canvas_height);
        img.format = ImageFormat::WebP;
        if (!img.pixels.resize(w * h * 3)) return std::nullopt;

        // Straight (non-premultiplied) alpha over black: dst = src * a.
        for (size_t px = 0; px < w * h; ++px) {
            const auto alpha = static_cast<unsigned>(rgba[(px * 4) + 3]);
            for (size_t ch = 0; ch < 3; ++ch) {
                const auto v = static_cast<unsigned>(rgba[(px * 4) + ch]);
                img.pixels[(px * 3) + ch] = static_cast<uint8_t>((v * alpha) / 255U);
            }
        }
        out = std::move(img);
    }

    WebPAnimDecoderDelete(dec);
    return out;
}

} // namespace

// Decode a WebP buffer to 3-channel RGB. libwebp validates the container and
// rejects truncated/corrupt input via WebPGetInfo / WebPDecodeRGBInto, so a bad
// buffer yields nullopt rather than a crash.
//
// An animated WebP carries ANIM/ANMF chunks instead of a top-level VP8/VP8L one,
// so WebPGetInfo reports the VP8X canvas size (passing the size guard below)
// while WebPDecodeRGBInto then fails — which used to make animated WebPs
// unimportable. Phase 57 routes those through the animation decoder's frame 0.
std::optional<ImageData> decode_webp_from_memory(std::span<const uint8_t> data)
{
    if (webp_is_animated(data)) {
        return decode_animated_webp_first_frame(data);
    }

    int w = 0;
    int h = 0;
    if (!WebPGetInfo(data.data(), data.size(), &w, &h) || w <= 0 || h <= 0)
        return std::nullopt;

    ImageData img;
    img.width  = w;
    img.height = h;
    img.format = ImageFormat::WebP;
    if (!img.pixels.resize(static_cast<size_t>(w) * h * 3)) return std::nullopt;

    if (const int stride = w * 3;
        !WebPDecodeRGBInto(data.data(), data.size(),
                           img.pixels.data(), img.pixels.size(), stride))
        return std::nullopt;

    return img;
}

namespace {

class WebpDecoder final : public Decoder {
public:
    [[nodiscard]] bool can_decode(std::span<const uint8_t> data) const noexcept override
    {
        return detect_format(data) == ImageFormat::WebP;
    }
    [[nodiscard]] std::optional<ImageData> decode(std::span<const uint8_t> data) const override
    {
        return decode_webp_from_memory(data);
    }
};

} // namespace

std::unique_ptr<Decoder> make_webp_decoder() { return std::make_unique<WebpDecoder>(); }

} // namespace image
