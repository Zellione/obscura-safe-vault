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
    std::vector<AVPacket*>  packets;  // Pre-read packets from the GIF
    size_t packet_idx = 0;            // Current packet index

    ~Impl() { close(); }

    void close()
    {
        if (sws) {
            sws_freeContext(sws);
            sws = nullptr;
        }
        if (frame) {
            av_frame_free(&frame);
        }
        if (pkt) {
            av_packet_free(&pkt);
        }
        if (codec) {
            avcodec_free_context(&codec);
        }
        if (fmt) {
            fmt->pb = nullptr;
            avformat_close_input(&fmt);
        }
        for (auto* p : packets) {
            av_packet_free(&p);
        }
        packets.clear();
        packet_idx = 0;
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
    if (!impl_->fmt) {
        return false;
    }
    impl_->fmt->pb = impl_->avio->ctx();

    // Force gif demuxer which handles animated GIFs.
    const AVInputFormat* gif_fmt = av_find_input_format("gif");
    if (!gif_fmt) {
        impl_->close();
        return false;
    }

    if (avformat_open_input(&impl_->fmt, nullptr, gif_fmt, nullptr) < 0) {
        impl_->fmt = nullptr;   // avformat_open_input freed it on failure
        impl_->close();
        return false;
    }
    if (avformat_find_stream_info(impl_->fmt, nullptr) < 0) {
        impl_->close();
        return false;
    }

    const AVCodec* dec = nullptr;
    impl_->stream_idx = av_find_best_stream(impl_->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (impl_->stream_idx < 0 || !dec) {
        impl_->close();
        return false;
    }

    AVStream* st = impl_->fmt->streams[impl_->stream_idx];
    impl_->time_base = st->time_base;

    impl_->codec = avcodec_alloc_context3(dec);
    if (!impl_->codec) {
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
    if (!impl_->frame || !impl_->pkt) {
        impl_->close();
        return false;
    }

    impl_->decoded = 0;
    impl_->eof_sent = false;
    impl_->packet_idx = 0;

    // Pre-read all packets from the GIF format context.
    AVPacket* temp_pkt = av_packet_alloc();
    if (!temp_pkt) {
        impl_->close();
        return false;
    }

    while (av_read_frame(impl_->fmt, temp_pkt) >= 0) {
        if (temp_pkt->stream_index == impl_->stream_idx) {
            // Clone the packet (deep copy, including packet data).
            // av_packet_ref would only shallow-copy, and the demuxer's
            // buffer gets reused on the next read.
            AVPacket* copy = av_packet_clone(temp_pkt);
            if (!copy) {
                av_packet_free(&temp_pkt);
                impl_->close();
                return false;
            }
            impl_->packets.push_back(copy);
        }
        av_packet_unref(temp_pkt);
    }
    av_packet_free(&temp_pkt);

    if (impl_->packets.empty()) {
        impl_->close();
        return false;
    }

    return true;
}

std::optional<GifFrame> GifDecoder::next_frame()
{
    if (!impl_->fmt || impl_->packets.empty()) {
        return std::nullopt;
    }

    // Check if we've exhausted all packets.
    if (impl_->packet_idx >= impl_->packets.size()) {
        return std::nullopt;
    }

    // Get the next packet.
    AVPacket* pkt = impl_->packets[impl_->packet_idx++];

    // For GIFs, create a fresh codec context for each frame to avoid decoder state issues.
    if (impl_->codec) {
        avcodec_free_context(&impl_->codec);
        impl_->codec = nullptr;
    }
    if (impl_->frame) {
        av_frame_free(&impl_->frame);
        impl_->frame = nullptr;
    }
    if (impl_->sws) {
        sws_freeContext(impl_->sws);
        impl_->sws = nullptr;
    }

    // Re-create codec context.
    const AVCodec* dec = nullptr;
    int stream_idx = av_find_best_stream(impl_->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (stream_idx < 0 || !dec) {
        return std::nullopt;
    }

    AVStream* st = impl_->fmt->streams[stream_idx];
    impl_->codec = avcodec_alloc_context3(dec);
    if (!impl_->codec) {
        return std::nullopt;
    }
    if (avcodec_parameters_to_context(impl_->codec, st->codecpar) < 0) {
        return std::nullopt;
    }

    // Open codec with options. For GIFs, we might need specific parameters.
    AVDictionary* opts = nullptr;
    if (avcodec_open2(impl_->codec, dec, &opts) < 0) {
        av_dict_free(&opts);
        return std::nullopt;
    }
    av_dict_free(&opts);

    impl_->frame = av_frame_alloc();
    if (!impl_->frame) {
        return std::nullopt;
    }

    // Send the packet and decode.
    int ret = avcodec_send_packet(impl_->codec, pkt);
    if (ret < 0) {
        return std::nullopt;
    }

    // Receive the decoded frame.
    ret = avcodec_receive_frame(impl_->codec, impl_->frame);
    if (ret != 0) {
        return std::nullopt;
    }

    // Successfully received a frame. Convert to RGBA.
    if (!impl_->frame->data[0]) {
        return std::nullopt;
    }

    // Lazily create the swscale context for RGBA conversion.
    if (!impl_->sws) {
        impl_->sws = sws_getCachedContext(
            impl_->sws,
            impl_->frame->width, impl_->frame->height,
            static_cast<AVPixelFormat>(impl_->frame->format),
            impl_->frame->width, impl_->frame->height,
            AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!impl_->sws) {
            return std::nullopt;
        }
    }

    // Allocate RGBA buffer: width * height * 4 bytes, tightly packed.
    const size_t rgba_size = static_cast<size_t>(impl_->frame->width)
                           * static_cast<size_t>(impl_->frame->height) * 4;
    std::vector<uint8_t> rgba(rgba_size);

    // Set up the destination frame pointers for sws_scale.
    uint8_t* dst_data[4] = {rgba.data(), nullptr, nullptr, nullptr};
    int dst_linesize[4] = {static_cast<int>(impl_->frame->width) * 4, 0, 0, 0};

    // Scale to RGBA.
    int scale_ret = sws_scale(impl_->sws,
                              impl_->frame->data,
                              impl_->frame->linesize,
                              0,
                              impl_->frame->height,
                              dst_data,
                              dst_linesize);
    if (scale_ret < 0) {
        return std::nullopt;
    }

    // Compute delay from frame duration and time base.
    // For GIFs, FFmpeg stores the frame delay in the frame's duration field.
    double delay_s = 0.0;
    if (impl_->frame->duration > 0) {
        delay_s = static_cast<double>(impl_->frame->duration)
                * av_q2d(impl_->time_base);
    }
    // Floor to kMinFrameDelay (also catches NaN).
    if (!(delay_s >= kMinFrameDelay)) {
        delay_s = kMinFrameDelay;
    }

    ++impl_->decoded;

    GifFrame result;
    result.rgba    = std::move(rgba);
    result.width   = impl_->frame->width;
    result.height  = impl_->frame->height;
    result.delay_s = delay_s;
    return result;
}

void GifDecoder::rewind()
{
    if (!impl_->codec) {
        return;
    }

    // Flush the codec buffers.
    avcodec_flush_buffers(impl_->codec);

    // Reset state for re-reading packets.
    impl_->eof_sent = false;
    impl_->packet_idx = 0;
    impl_->decoded = 0;
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
