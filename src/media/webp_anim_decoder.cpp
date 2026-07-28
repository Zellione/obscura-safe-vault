#include "media/webp_anim_decoder.h"

#include <webp/decode.h>
#include <webp/demux.h>

namespace media {

double webp_frame_delay_s(int prev_timestamp_ms, int timestamp_ms) noexcept
{
    const int delta_ms = timestamp_ms - prev_timestamp_ms;
    if (delta_ms <= 0) {
        return kMinFrameDelay;      // 0 ms or non-monotonic: use the floor
    }
    const double delay_s = static_cast<double>(delta_ms) / 1000.0;
    return delay_s >= kMinFrameDelay ? delay_s : kMinFrameDelay;
}

struct WebpAnimDecoder::Impl {
    ::WebPAnimDecoder* dec     = nullptr;
    int                width   = 0;
    int                height  = 0;
    size_t             decoded = 0;
    int                prev_ts = 0;

    ~Impl() { close(); }

    void close() noexcept
    {
        if (dec != nullptr) {
            WebPAnimDecoderDelete(dec);
            dec = nullptr;
        }
    }
};

WebpAnimDecoder::WebpAnimDecoder() : impl_(std::make_unique<Impl>()) {}

WebpAnimDecoder::~WebpAnimDecoder() = default;

bool WebpAnimDecoder::open(std::span<const uint8_t> data)
{
    impl_ = std::make_unique<Impl>();       // drop any previous animation

    if (data.empty()) {
        return false;
    }

    const WebPData webp_data{.bytes = data.data(), .size = data.size()};

    WebPAnimDecoderOptions opts;
    if (WebPAnimDecoderOptionsInit(&opts) == 0) {
        return false;
    }
    opts.color_mode  = MODE_RGBA;
    opts.use_threads = 0;

    // Borrows `data`: WebPAnimDecoderNew keeps the pointer, so the caller's
    // buffer must outlive this decoder.
    impl_->dec = WebPAnimDecoderNew(&webp_data, &opts);
    if (impl_->dec == nullptr) {
        return false;
    }

    WebPAnimInfo info{};
    if (WebPAnimDecoderGetInfo(impl_->dec, &info) == 0 || info.frame_count < 2
        || info.canvas_width == 0 || info.canvas_height == 0) {
        impl_->close();                     // static or degenerate: not an animation
        return false;
    }

    impl_->width  = static_cast<int>(info.canvas_width);
    impl_->height = static_cast<int>(info.canvas_height);
    return true;
}

std::optional<AnimFrame> WebpAnimDecoder::next_frame()
{
    if (impl_->dec == nullptr || WebPAnimDecoderHasMoreFrames(impl_->dec) == 0) {
        return std::nullopt;
    }

    uint8_t* rgba         = nullptr;
    int      timestamp_ms = 0;
    if (WebPAnimDecoderGetNext(impl_->dec, &rgba, &timestamp_ms) == 0 || rgba == nullptr) {
        return std::nullopt;
    }

    const auto w = static_cast<size_t>(impl_->width);
    const auto h = static_cast<size_t>(impl_->height);

    AnimFrame frame;
    frame.width   = impl_->width;
    frame.height  = impl_->height;
    frame.delay_s = webp_frame_delay_s(impl_->prev_ts, timestamp_ms);
    frame.rgba.resize(w * h * 4);

    // libwebp's pointer is only valid until the next GetNext/Reset call, so copy
    // it out. Alpha is flattened over black in the same pass: the app carries no
    // transparency (ImageData is RGB, thumbnails are JPEG), and emitting opaque
    // pixels keeps the texture upload free of any blend-mode change.
    for (size_t px = 0; px < w * h; ++px) {
        const auto alpha = static_cast<unsigned>(rgba[(px * 4) + 3]);
        for (size_t ch = 0; ch < 3; ++ch) {
            const auto v = static_cast<unsigned>(rgba[(px * 4) + ch]);
            frame.rgba[(px * 4) + ch] = static_cast<uint8_t>((v * alpha) / 255U);
        }
        frame.rgba[(px * 4) + 3] = 0xFF;
    }

    impl_->prev_ts = timestamp_ms;
    ++impl_->decoded;
    return frame;
}

void WebpAnimDecoder::rewind()
{
    if (impl_->dec == nullptr) {
        return;
    }
    WebPAnimDecoderReset(impl_->dec);
    impl_->decoded = 0;
    impl_->prev_ts = 0;
}

int    WebpAnimDecoder::width()  const noexcept { return impl_->width; }
int    WebpAnimDecoder::height() const noexcept { return impl_->height; }
size_t WebpAnimDecoder::frames_decoded() const noexcept { return impl_->decoded; }

} // namespace media
