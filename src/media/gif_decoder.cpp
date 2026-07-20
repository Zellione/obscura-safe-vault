#include "media/gif_decoder.h"

#ifdef OSV_VENDORED_AV

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

#include <print>

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
    AVRational       time_base  {1, 100};
    size_t           decoded    = 0;
    bool             eof_sent   = false;
    bool             demux_eof  = false;  // Track if demuxer has reached EOF

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
    // Note: Skip avformat_find_stream_info() as it may corrupt packet state for GIFs.
    // We get enough info from av_find_best_stream() without it.

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
    impl_->demux_eof = false;

    // Note: Do NOT pre-read packets! The GIF demuxer's internal state
    // gets corrupted if we read until EOF and then try to use cloned packets.
    // Instead, we'll read packets on-demand in next_frame().
    // The pkt_ is allocated above and reused for reading.

    return true;
}

std::optional<GifFrame> GifDecoder::next_frame()
{
    if (!impl_->fmt) {
        return std::nullopt;
    }

    // Read next packet from demuxer (skip non-video packets)
    while (true) {
        if (impl_->demux_eof) {
            return std::nullopt;  // No more packets
        }

        int ret = av_read_frame(impl_->fmt, impl_->pkt);
        if (ret < 0) {
            impl_->demux_eof = true;
            return std::nullopt;
        }

        if (impl_->pkt->stream_index == impl_->stream_idx) {
            break;  // Got a video packet
        }

        av_packet_unref(impl_->pkt);
    }

    // We have a video packet - create a fresh codec context for this packet
    // (GIF packets seem to be independent and need separate decoder instances)
    const AVCodec* dec = nullptr;
    int stream_idx = av_find_best_stream(impl_->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (stream_idx < 0 || dec == nullptr) {
        av_packet_unref(impl_->pkt);
        return std::nullopt;
    }

    AVCodecContext* temp_ctx = avcodec_alloc_context3(dec);
    if (temp_ctx == nullptr) {
        av_packet_unref(impl_->pkt);
        return std::nullopt;
    }

    AVStream* st = impl_->fmt->streams[stream_idx];
    if (avcodec_parameters_to_context(temp_ctx, st->codecpar) < 0) {
        avcodec_free_context(&temp_ctx);
        av_packet_unref(impl_->pkt);
        return std::nullopt;
    }

    if (avcodec_open2(temp_ctx, dec, nullptr) < 0) {
        avcodec_free_context(&temp_ctx);
        av_packet_unref(impl_->pkt);
        return std::nullopt;
    }

    // Allocate a frame for this packet's decoding
    AVFrame* temp_frame = av_frame_alloc();
    if (temp_frame == nullptr) {
        avcodec_free_context(&temp_ctx);
        av_packet_unref(impl_->pkt);
        return std::nullopt;
    }

    // Send packet and receive frame
    int ret = avcodec_send_packet(temp_ctx, impl_->pkt);
    av_packet_unref(impl_->pkt);

    if (ret < 0) {
        av_frame_free(&temp_frame);
        avcodec_free_context(&temp_ctx);
        return std::nullopt;
    }

    ret = avcodec_receive_frame(temp_ctx, temp_frame);
    if (ret != 0) {
        av_frame_free(&temp_frame);
        avcodec_free_context(&temp_ctx);
        return std::nullopt;
    }

    // Successfully got a frame from the packet
    // Copy frame data before freeing the context
    if (!temp_frame->data[0]) {
        av_frame_free(&temp_frame);
        avcodec_free_context(&temp_ctx);
        return std::nullopt;
    }

    // Lazy create swscale context (using persistent sws_)
    if (impl_->sws == nullptr) {
        impl_->sws = sws_getCachedContext(
            impl_->sws,
            temp_frame->width, temp_frame->height,
            static_cast<AVPixelFormat>(temp_frame->format),
            temp_frame->width, temp_frame->height,
            AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (impl_->sws == nullptr) {
            av_frame_free(&temp_frame);
            avcodec_free_context(&temp_ctx);
            return std::nullopt;
        }
    }

    // Allocate RGBA buffer
    const size_t rgba_size = static_cast<size_t>(temp_frame->width)
                           * static_cast<size_t>(temp_frame->height) * 4;
    std::vector<uint8_t> rgba(rgba_size);

    // Set up destination frame pointers
    uint8_t* dst_data[4] = {rgba.data(), nullptr, nullptr, nullptr};
    int dst_linesize[4] = {static_cast<int>(temp_frame->width) * 4, 0, 0, 0};

    // Scale to RGBA
    int scale_ret = sws_scale(impl_->sws,
                              temp_frame->data,
                              temp_frame->linesize,
                              0,
                              temp_frame->height,
                              dst_data,
                              dst_linesize);

    // Extract duration from the temporary frame before cleanup
    int64_t frame_duration = temp_frame->duration;
    int frame_width = temp_frame->width;
    int frame_height = temp_frame->height;

    // Cleanup temporary resources
    av_frame_free(&temp_frame);
    avcodec_free_context(&temp_ctx);

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
    if (!impl_->fmt || !impl_->avio) {
        return;
    }

    // Reset state for re-reading packets
    impl_->demux_eof = false;
    impl_->decoded = 0;

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
