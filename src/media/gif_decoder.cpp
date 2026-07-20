#include "media/gif_decoder.h"

#ifdef OSV_VENDORED_AV

#include <array>
#include <memory>

#include "media/mem_avio.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace media {

struct GifDecoder::Impl {
    std::unique_ptr<MemAvio> avio;
    AVFormatContext* fmt        = nullptr;
    AVCodecContext*  codec      = nullptr;
    SwsContext*      sws        = nullptr;
    AVFrame*         frame      = nullptr;
    AVPacket*        pkt        = nullptr;
    int              stream_idx = -1;
    int              width      = 0;
    int              height     = 0;
    AVRational       time_base  {.num = 1, .den = 100};
    size_t           decoded    = 0;
    bool             eof_sent   = false;

    ~Impl() { close(); }

    void close()
    {
        if (sws != nullptr) {
            sws_freeContext(sws);
            sws = nullptr;
        }
        if (frame != nullptr) {
            av_frame_free(&frame);
        }
        if (pkt != nullptr) {
            av_packet_free(&pkt);
        }
        if (codec != nullptr) {
            avcodec_free_context(&codec);
        }
        if (fmt != nullptr) {
            fmt->pb = nullptr;
            avformat_close_input(&fmt);
        }
        avio.reset();
    }
};

GifDecoder::GifDecoder() : impl_(std::make_unique<Impl>()) {}

GifDecoder::~GifDecoder() = default;

bool GifDecoder::open(std::span<const uint8_t> data)
{
    // Reset: destroy old impl, create a fresh one.
    impl_ = std::make_unique<Impl>();

    impl_->avio = std::make_unique<MemAvio>(data);
    if (!impl_->avio->valid()) {
        return false;
    }

    impl_->fmt = avformat_alloc_context();
    if (impl_->fmt == nullptr) {
        return false;
    }
    impl_->fmt->pb = impl_->avio->ctx();

    // Force gif demuxer which handles animated GIFs.
    const AVInputFormat* gif_fmt = av_find_input_format("gif");
    if (gif_fmt == nullptr) {
        impl_->close();
        return false;
    }

    if (avformat_open_input(&impl_->fmt, nullptr, gif_fmt, nullptr) < 0) {
        impl_->fmt = nullptr;   // avformat_open_input freed it on failure
        impl_->close();
        return false;
    }
    // Note: avformat_find_stream_info() is unnecessary here. The GIF demuxer's
    // read_header already populates codecpar and time_base, so the decoder opens
    // correctly without the additional probe.

    const AVCodec* dec = nullptr;
    impl_->stream_idx = av_find_best_stream(impl_->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (impl_->stream_idx < 0 || dec == nullptr) {
        impl_->close();
        return false;
    }

    AVStream* st = impl_->fmt->streams[impl_->stream_idx];
    impl_->time_base = st->time_base;

    impl_->codec = avcodec_alloc_context3(dec);
    if (impl_->codec == nullptr) {
        impl_->close();
        return false;
    }
    if (avcodec_parameters_to_context(impl_->codec, st->codecpar) < 0) {
        impl_->close();
        return false;
    }
    if (avcodec_open2(impl_->codec, dec, nullptr) < 0) {
        // Try opening without a codec context (let FFmpeg infer it)
        impl_->close();
        return false;
    }

    impl_->width  = impl_->codec->width;
    impl_->height = impl_->codec->height;
    if (impl_->width <= 0 || impl_->height <= 0) {
        impl_->close();
        return false;
    }

    impl_->frame = av_frame_alloc();
    impl_->pkt   = av_packet_alloc();
    if (impl_->frame == nullptr || impl_->pkt == nullptr) {
        impl_->close();
        return false;
    }

    impl_->decoded = 0;
    impl_->eof_sent = false;

    // Note: Do NOT pre-read packets! The GIF demuxer's internal state
    // gets corrupted if we read until EOF and then try to use cloned packets.
    // Instead, we'll read packets on-demand in next_frame().
    // The pkt_ is allocated above and reused for reading.

    return true;
}

std::optional<GifFrame> GifDecoder::next_frame()
{
    if (impl_->fmt == nullptr || impl_->codec == nullptr || impl_->frame == nullptr || impl_->pkt == nullptr) {
        return std::nullopt;
    }

    // Main loop: read packets and try to receive frames
    while (true) {
        // Try to receive a frame from the decoder (non-blocking)
        const int recv_ret = avcodec_receive_frame(impl_->codec, impl_->frame);
        if (recv_ret == 0) {
            // Successfully got a frame from the decoder
            break;  // Exit loop with a valid frame in impl_->frame
        }
        if (recv_ret != AVERROR(EAGAIN)) {
            // Any error other than EAGAIN means we're done (including EOF after flush)
            return std::nullopt;
        }

        // EAGAIN: decoder wants more input. Read next packet from demuxer.
        const int read_ret = av_read_frame(impl_->fmt, impl_->pkt);
        if (read_ret == AVERROR_EOF) {
            // Reached end of stream: send flush packet if we haven't already
            if (!impl_->eof_sent) {
                impl_->eof_sent = true;
                avcodec_send_packet(impl_->codec, nullptr);  // nullptr = flush
                continue;  // Loop to try receiving buffered frames
            }
            // Already flushed and got EAGAIN again: no more frames
            return std::nullopt;
        }
        if (read_ret < 0) {
            // Read error
            return std::nullopt;
        }

        // Route the packet to the decoder if it's video
        if (impl_->pkt->stream_index == impl_->stream_idx) {
            const int send_ret = avcodec_send_packet(impl_->codec, impl_->pkt);
            if (send_ret < 0) {
                av_packet_unref(impl_->pkt);
                return std::nullopt;
            }
        }
        av_packet_unref(impl_->pkt);
        // Loop back to try receiving a frame
    }

    // We have a valid frame in impl_->frame
    if (impl_->frame->data[0] == nullptr) {
        return std::nullopt;
    }

    // Lazy create swscale context
    if (impl_->sws == nullptr) {
        impl_->sws = sws_getCachedContext(
            impl_->sws,
            impl_->frame->width, impl_->frame->height,
            static_cast<AVPixelFormat>(impl_->frame->format),
            impl_->frame->width, impl_->frame->height,
            AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (impl_->sws == nullptr) {
            return std::nullopt;
        }
    }

    // Allocate RGBA buffer
    const size_t rgba_size = static_cast<size_t>(impl_->frame->width)
                           * static_cast<size_t>(impl_->frame->height) * 4;
    std::vector<uint8_t> rgba(rgba_size);

    // Set up destination frame pointers
    std::array<uint8_t*, 4> dst_data = {rgba.data(), nullptr, nullptr, nullptr};
    std::array<int, 4> dst_linesize = {impl_->frame->width * 4, 0, 0, 0};

    // Scale to RGBA
    const int scale_ret = sws_scale(impl_->sws,
                                    impl_->frame->data,
                                    impl_->frame->linesize,
                                    0,
                                    impl_->frame->height,
                                    dst_data.data(),
                                    dst_linesize.data());

    // Extract duration from the frame
    const int64_t frame_duration = impl_->frame->duration;
    const int frame_width = impl_->frame->width;
    const int frame_height = impl_->frame->height;

    if (scale_ret < 0) {
        return std::nullopt;
    }

    // Compute delay from frame duration and time base
    double delay_s = 0.0;
    if (frame_duration > 0) {
        delay_s = static_cast<double>(frame_duration)
                * av_q2d(impl_->time_base);
    }
    // Floor to kMinFrameDelay (also catches NaN)
    if (!(delay_s >= kMinFrameDelay)) {
        delay_s = kMinFrameDelay;
    }

    ++impl_->decoded;

    GifFrame result;
    result.rgba    = std::move(rgba);
    result.width   = frame_width;
    result.height  = frame_height;
    result.delay_s = delay_s;
    return result;
}

void GifDecoder::rewind()
{
    if (impl_->fmt == nullptr || !impl_->avio || impl_->codec == nullptr) {
        return;
    }

    // Reset state for re-reading packets
    impl_->eof_sent = false;
    impl_->decoded = 0;

    // Flush the codec to discard any buffered frames
    avcodec_flush_buffers(impl_->codec);

    // Seek the format context back to the beginning
    avformat_seek_file(impl_->fmt, impl_->stream_idx, 0, 0, INT64_MAX,
                       AVSEEK_FLAG_BACKWARD);
}

int GifDecoder::width() const noexcept
{
    return impl_->width;
}

int GifDecoder::height() const noexcept
{
    return impl_->height;
}

size_t GifDecoder::frames_decoded() const noexcept
{
    return impl_->decoded;
}

} // namespace media

#endif // OSV_VENDORED_AV
