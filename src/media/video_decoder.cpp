#ifdef OSV_VENDORED_AV

#include "media/video_decoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

#include <cstdio>

namespace media {

VideoDecoder::VideoDecoder() = default;

VideoDecoder::~VideoDecoder()
{
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }
    if (fmt_) {
        avformat_close_input(&fmt_);  // frees fmt_ itself
    }
    if (frame_) {
        av_frame_free(&frame_);
    }
    if (pkt_) {
        av_packet_free(&pkt_);
    }
}

VideoDecoder::VideoDecoder(VideoDecoder&& other) noexcept
    : fmt_(other.fmt_),
      codec_ctx_(other.codec_ctx_),
      frame_(other.frame_),
      pkt_(other.pkt_),
      stream_index_(other.stream_index_),
      width_(other.width_),
      height_(other.height_),
      duration_us_(other.duration_us_),
      codec_(other.codec_),
      time_base_(other.time_base_),
      flushed_(other.flushed_)
{
    other.fmt_        = nullptr;
    other.codec_ctx_  = nullptr;
    other.frame_      = nullptr;
    other.pkt_        = nullptr;
}

VideoDecoder& VideoDecoder::operator=(VideoDecoder&& other) noexcept
{
    if (this != &other) {
        // Clean up existing state
        if (codec_ctx_) {
            avcodec_free_context(&codec_ctx_);
        }
        if (fmt_) {
            avformat_close_input(&fmt_);
        }
        if (frame_) {
            av_frame_free(&frame_);
        }
        if (pkt_) {
            av_packet_free(&pkt_);
        }

        // Move from other
        fmt_        = other.fmt_;
        codec_ctx_  = other.codec_ctx_;
        frame_      = other.frame_;
        pkt_        = other.pkt_;
        stream_index_ = other.stream_index_;
        width_      = other.width_;
        height_     = other.height_;
        duration_us_ = other.duration_us_;
        codec_      = other.codec_;
        time_base_  = other.time_base_;
        flushed_    = other.flushed_;

        other.fmt_        = nullptr;
        other.codec_ctx_  = nullptr;
        other.frame_      = nullptr;
        other.pkt_        = nullptr;
    }
    return *this;
}

bool VideoDecoder::open(AVIOContext* pb)
{
    if (!pb) {
        std::fprintf(stderr, "[VideoDecoder] AVIO context is null\n");
        return false;
    }

    // Allocate format context and assign the I/O context
    fmt_ = avformat_alloc_context();
    if (!fmt_) {
        std::fprintf(stderr, "[VideoDecoder] Failed to allocate format context\n");
        return false;
    }

    fmt_->pb = pb;

    // Open input (NULL url since pb provides the data)
    int ret = avformat_open_input(&fmt_, nullptr, nullptr, nullptr);
    if (ret < 0) {
        std::fprintf(stderr, "[VideoDecoder] avformat_open_input failed: %d\n", ret);
        // avformat_open_input frees fmt_ on failure; null it
        fmt_ = nullptr;
        return false;
    }

    // Find stream info
    ret = avformat_find_stream_info(fmt_, nullptr);
    if (ret < 0) {
        std::fprintf(stderr, "[VideoDecoder] avformat_find_stream_info failed: %d\n", ret);
        avformat_close_input(&fmt_);
        fmt_ = nullptr;
        return false;
    }

    // Find best video stream
    const AVCodec* decoder = nullptr;
    stream_index_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_VIDEO, -1, -1,
                                        &decoder, 0);
    if (stream_index_ < 0) {
        std::fprintf(stderr, "[VideoDecoder] No video stream found\n");
        avformat_close_input(&fmt_);
        fmt_ = nullptr;
        return false;
    }

    if (!decoder) {
        std::fprintf(stderr, "[VideoDecoder] No decoder found\n");
        avformat_close_input(&fmt_);
        fmt_ = nullptr;
        return false;
    }

    AVStream* stream = fmt_->streams[stream_index_];
    if (!stream || !stream->codecpar) {
        std::fprintf(stderr, "[VideoDecoder] Invalid stream or codecpar\n");
        avformat_close_input(&fmt_);
        fmt_ = nullptr;
        return false;
    }

    // Map codec ID to VideoCodec
    switch (stream->codecpar->codec_id) {
        case AV_CODEC_ID_H264:
            codec_ = vault::VideoCodec::H264;
            break;
        case AV_CODEC_ID_HEVC:
            codec_ = vault::VideoCodec::HEVC;
            break;
        default:
            std::fprintf(stderr, "[VideoDecoder] Unsupported codec ID: %d\n",
                        stream->codecpar->codec_id);
            avformat_close_input(&fmt_);
            fmt_ = nullptr;
            return false;
    }

    // Allocate and fill codec context
    codec_ctx_ = avcodec_alloc_context3(decoder);
    if (!codec_ctx_) {
        std::fprintf(stderr, "[VideoDecoder] Failed to allocate codec context\n");
        avformat_close_input(&fmt_);
        fmt_ = nullptr;
        return false;
    }

    ret = avcodec_parameters_to_context(codec_ctx_, stream->codecpar);
    if (ret < 0) {
        std::fprintf(stderr, "[VideoDecoder] avcodec_parameters_to_context failed: %d\n", ret);
        avcodec_free_context(&codec_ctx_);
        avformat_close_input(&fmt_);
        fmt_ = nullptr;
        return false;
    }

    // Open the codec
    ret = avcodec_open2(codec_ctx_, decoder, nullptr);
    if (ret < 0) {
        std::fprintf(stderr, "[VideoDecoder] avcodec_open2 failed: %d\n", ret);
        avcodec_free_context(&codec_ctx_);
        avformat_close_input(&fmt_);
        fmt_ = nullptr;
        return false;
    }

    // Cache dimensions
    width_  = codec_ctx_->width;
    height_ = codec_ctx_->height;

    // Cache time base for PTS calculation
    time_base_ = av_q2d(stream->time_base);

    // Cache duration in microseconds
    // Prefer fmt_->duration (in AV_TIME_BASE units = microseconds)
    if (fmt_->duration > 0 && fmt_->duration != AV_NOPTS_VALUE) {
        duration_us_ = static_cast<uint64_t>(fmt_->duration);
    } else if (stream->duration > 0 && stream->duration != AV_NOPTS_VALUE) {
        // Fall back to stream duration
        duration_us_ = static_cast<uint64_t>(stream->duration * av_q2d(stream->time_base) * 1e6);
    } else {
        duration_us_ = 0;
    }

    // Allocate frame and packet
    frame_ = av_frame_alloc();
    if (!frame_) {
        std::fprintf(stderr, "[VideoDecoder] Failed to allocate frame\n");
        avcodec_free_context(&codec_ctx_);
        avformat_close_input(&fmt_);
        fmt_ = nullptr;
        return false;
    }

    pkt_ = av_packet_alloc();
    if (!pkt_) {
        std::fprintf(stderr, "[VideoDecoder] Failed to allocate packet\n");
        av_frame_free(&frame_);
        avcodec_free_context(&codec_ctx_);
        avformat_close_input(&fmt_);
        fmt_ = nullptr;
        return false;
    }

    return true;
}

std::optional<DecodedFrame> VideoDecoder::next_frame()
{
    if (!codec_ctx_ || !frame_ || !pkt_) {
        return std::nullopt;
    }

    while (true) {
        // Try to get a decoded frame
        int ret = avcodec_receive_frame(codec_ctx_, frame_);

        if (ret == 0) {
            // Got a frame; check if pixel format is supported
            FramePixelFormat pix_fmt;
            switch (frame_->format) {
                case AV_PIX_FMT_YUV420P:
                    pix_fmt = FramePixelFormat::I420;
                    break;
                case AV_PIX_FMT_NV12:
                    pix_fmt = FramePixelFormat::NV12;
                    break;
                default:
                    // Unsupported format in Task 1; Task 2 will add swscale conversion
                    std::fprintf(stderr, "[VideoDecoder] Unsupported pixel format: %d\n",
                                frame_->format);
                    return std::nullopt;
            }

            // Calculate PTS
            double pts_seconds = 0.0;
            if (frame_->best_effort_timestamp != AV_NOPTS_VALUE) {
                pts_seconds = frame_->best_effort_timestamp * time_base_;
            }

            return DecodedFrame{
                .planes    = {frame_->data[0], frame_->data[1], frame_->data[2]},
                .linesizes = {frame_->linesize[0], frame_->linesize[1], frame_->linesize[2]},
                .width     = frame_->width,
                .height    = frame_->height,
                .pix_fmt   = pix_fmt,
                .pts_seconds = pts_seconds,
            };
        } else if (ret == AVERROR(EAGAIN)) {
            // Decoder needs more input
            if (flushed_) {
                // Already sent flush packet and got EAGAIN again; end of stream
                return std::nullopt;
            }

            // Try to read a packet
            ret = av_read_frame(fmt_, pkt_);
            if (ret == AVERROR_EOF) {
                // EOF: send flush packet and loop to drain
                flushed_ = true;
                avcodec_send_packet(codec_ctx_, nullptr);  // nullptr = flush
                continue;
            } else if (ret < 0) {
                // Read error
                std::fprintf(stderr, "[VideoDecoder] av_read_frame failed: %d\n", ret);
                return std::nullopt;
            }

            // Only send our video stream packets
            if (pkt_->stream_index == stream_index_) {
                ret = avcodec_send_packet(codec_ctx_, pkt_);
                if (ret < 0) {
                    std::fprintf(stderr, "[VideoDecoder] avcodec_send_packet failed: %d\n", ret);
                    av_packet_unref(pkt_);
                    return std::nullopt;
                }
            }

            av_packet_unref(pkt_);
            // Loop to try avcodec_receive_frame again
        } else if (ret == AVERROR_EOF) {
            // No more frames
            return std::nullopt;
        } else {
            // Some other error
            std::fprintf(stderr, "[VideoDecoder] avcodec_receive_frame failed: %d\n", ret);
            return std::nullopt;
        }
    }
}

} // namespace media

#endif // OSV_VENDORED_AV
