#ifdef OSV_VENDORED_AV

#include "media/video_decoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <cstdio>
#include <cstring>

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
    if (sws_) {
        sws_freeContext(sws_);
    }
    if (conv_) {
        av_frame_free(&conv_);
    }
}

VideoDecoder::VideoDecoder(VideoDecoder&& other) noexcept
    : fmt_(other.fmt_),
      codec_ctx_(other.codec_ctx_),
      frame_(other.frame_),
      pkt_(other.pkt_),
      sws_(other.sws_),
      conv_(other.conv_),
      stream_index_(other.stream_index_),
      width_(other.width_),
      height_(other.height_),
      duration_us_(other.duration_us_),
      codec_(other.codec_),
      time_base_(other.time_base_),
      stream_time_base_(other.stream_time_base_),
      flushed_(other.flushed_),
      pending_seek_target_(other.pending_seek_target_)
{
    other.fmt_        = nullptr;
    other.codec_ctx_  = nullptr;
    other.frame_      = nullptr;
    other.pkt_        = nullptr;
    other.sws_        = nullptr;
    other.conv_       = nullptr;
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
        if (sws_) {
            sws_freeContext(sws_);
        }
        if (conv_) {
            av_frame_free(&conv_);
        }

        // Move from other
        fmt_        = other.fmt_;
        codec_ctx_  = other.codec_ctx_;
        frame_      = other.frame_;
        pkt_        = other.pkt_;
        sws_        = other.sws_;
        conv_       = other.conv_;
        stream_index_ = other.stream_index_;
        width_      = other.width_;
        height_     = other.height_;
        duration_us_ = other.duration_us_;
        codec_      = other.codec_;
        time_base_  = other.time_base_;
        stream_time_base_ = other.stream_time_base_;
        flushed_    = other.flushed_;
        pending_seek_target_ = other.pending_seek_target_;

        other.fmt_        = nullptr;
        other.codec_ctx_  = nullptr;
        other.frame_      = nullptr;
        other.pkt_        = nullptr;
        other.sws_        = nullptr;
        other.conv_       = nullptr;
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

    // Cache raw stream time base for seek calculations
    stream_time_base_ = stream->time_base;

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

bool VideoDecoder::seek(double ts_seconds)
{
    if (!fmt_ || stream_index_ < 0) {
        std::fprintf(stderr, "[VideoDecoder] Cannot seek: not opened\n");
        return false;
    }

    // Convert ts_seconds to stream time base units
    // ts_seconds * (1 / time_base) = timestamp in stream units
    int64_t ts = av_rescale_q(
        static_cast<int64_t>(ts_seconds * AV_TIME_BASE),
        AV_TIME_BASE_Q,
        stream_time_base_
    );

    // Perform keyframe-anchored seek (backward to nearest keyframe)
    int ret = av_seek_frame(fmt_, stream_index_, ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        std::fprintf(stderr, "[VideoDecoder] av_seek_frame failed: %d\n", ret);
        return false;
    }

    // Flush the codec so it discards any buffered frames
    avcodec_flush_buffers(codec_ctx_);

    // Reset decode state
    flushed_ = false;

    // Set the pending seek target so next_frame() will decode forward until reaching it
    pending_seek_target_ = ts_seconds;

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
            // Got a frame; check pixel format and convert if needed
            double pts_seconds = 0.0;
            if (frame_->best_effort_timestamp != AV_NOPTS_VALUE) {
                pts_seconds = frame_->best_effort_timestamp * time_base_;
            }

            // If we're seeking, skip frames until we reach the target
            if (pending_seek_target_ >= 0.0 && pts_seconds < pending_seek_target_) {
                // Keep decoding to reach the target
                continue;
            }

            // Clear pending seek target once we've reached it
            if (pending_seek_target_ >= 0.0) {
                pending_seek_target_ = -1.0;
            }

            // Handle pixel format conversion
            FramePixelFormat pix_fmt;
            const uint8_t* out_planes[3];
            int out_linesizes[3];

            switch (frame_->format) {
                case AV_PIX_FMT_YUV420P:
                    // Direct pass-through: already I420
                    pix_fmt = FramePixelFormat::I420;
                    out_planes[0] = frame_->data[0];
                    out_planes[1] = frame_->data[1];
                    out_planes[2] = frame_->data[2];
                    out_linesizes[0] = frame_->linesize[0];
                    out_linesizes[1] = frame_->linesize[1];
                    out_linesizes[2] = frame_->linesize[2];
                    break;

                case AV_PIX_FMT_NV12:
                    // Direct pass-through: already NV12
                    pix_fmt = FramePixelFormat::NV12;
                    out_planes[0] = frame_->data[0];
                    out_planes[1] = frame_->data[1];
                    out_planes[2] = nullptr;
                    out_linesizes[0] = frame_->linesize[0];
                    out_linesizes[1] = frame_->linesize[1];
                    out_linesizes[2] = 0;
                    break;

                default:
                    // Unsupported format: use swscale to convert to YUV420P
                    if (!sws_) {
                        // Lazy initialization of swscale context
                        sws_ = sws_getCachedContext(
                            sws_,
                            frame_->width,
                            frame_->height,
                            static_cast<AVPixelFormat>(frame_->format),
                            frame_->width,
                            frame_->height,
                            AV_PIX_FMT_YUV420P,
                            SWS_BILINEAR,
                            nullptr,
                            nullptr,
                            nullptr
                        );
                        if (!sws_) {
                            std::fprintf(stderr, "[VideoDecoder] Failed to create swscale context\n");
                            return std::nullopt;
                        }
                    }

                    // Ensure conv_ is allocated with proper dimensions
                    if (!conv_) {
                        conv_ = av_frame_alloc();
                        if (!conv_) {
                            std::fprintf(stderr, "[VideoDecoder] Failed to allocate conversion frame\n");
                            return std::nullopt;
                        }
                    }

                    // Set up the conversion frame for YUV420P
                    conv_->format = AV_PIX_FMT_YUV420P;
                    conv_->width = frame_->width;
                    conv_->height = frame_->height;

                    // Allocate or reallocate buffer if dimensions changed
                    int buf_ret = av_frame_get_buffer(conv_, 0);
                    if (buf_ret < 0) {
                        std::fprintf(stderr, "[VideoDecoder] av_frame_get_buffer failed: %d\n", buf_ret);
                        return std::nullopt;
                    }

                    // Perform the scale/convert
                    ret = sws_scale(sws_,
                                   frame_->data,
                                   frame_->linesize,
                                   0,
                                   frame_->height,
                                   conv_->data,
                                   conv_->linesize);
                    if (ret < 0) {
                        std::fprintf(stderr, "[VideoDecoder] sws_scale failed: %d\n", ret);
                        return std::nullopt;
                    }

                    pix_fmt = FramePixelFormat::I420;
                    out_planes[0] = conv_->data[0];
                    out_planes[1] = conv_->data[1];
                    out_planes[2] = conv_->data[2];
                    out_linesizes[0] = conv_->linesize[0];
                    out_linesizes[1] = conv_->linesize[1];
                    out_linesizes[2] = conv_->linesize[2];
                    break;
            }

            return DecodedFrame{
                .planes    = {out_planes[0], out_planes[1], out_planes[2]},
                .linesizes = {out_linesizes[0], out_linesizes[1], out_linesizes[2]},
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
                // Read error — terminate gracefully
                return std::nullopt;
            }

            // Only send our video stream packets
            if (pkt_->stream_index == stream_index_) {
                ret = avcodec_send_packet(codec_ctx_, pkt_);
                if (ret < 0) {
                    // Decode error — terminate gracefully
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
            // Some other error — terminate gracefully
            return std::nullopt;
        }
    }
}

std::optional<image::ImageData> VideoDecoder::decode_poster_rgb()
{
    if (!frame_ || !codec_ctx_) {
        return std::nullopt;
    }

    // Seek to the start if not already there.
    if (!seek(0.0)) {
        // If seek fails, try to decode from current position anyway.
    }

    // Decode the first frame.
    auto decoded = next_frame();
    if (!decoded) {
        return std::nullopt;
    }

    const int w = width_;
    const int h = height_;
    if (w <= 0 || h <= 0) {
        return std::nullopt;
    }

    // Create a temporary swscale context for conversion to RGB24.
    // We allocate a fresh context (not cached) so we can cleanly free it.
    SwsContext* sws_rgb = sws_alloc_context();
    if (!sws_rgb) {
        return std::nullopt;
    }

    // Set conversion parameters.
    av_opt_set_int(sws_rgb, "srcw", w, 0);
    av_opt_set_int(sws_rgb, "srch", h, 0);
    av_opt_set_int(sws_rgb, "src_format", static_cast<int>(frame_->format), 0);
    av_opt_set_int(sws_rgb, "dstw", w, 0);
    av_opt_set_int(sws_rgb, "dsth", h, 0);
    av_opt_set_int(sws_rgb, "dst_format", static_cast<int>(AV_PIX_FMT_RGB24), 0);
    av_opt_set_int(sws_rgb, "flags", SWS_BILINEAR, 0);

    if (sws_init_context(sws_rgb, nullptr, nullptr) < 0) {
        sws_freeContext(sws_rgb);
        return std::nullopt;
    }

    // Allocate destination frame for RGB24.
    AVFrame* rgb_frame = av_frame_alloc();
    if (!rgb_frame) {
        sws_freeContext(sws_rgb);
        return std::nullopt;
    }

    rgb_frame->format = AV_PIX_FMT_RGB24;
    rgb_frame->width  = w;
    rgb_frame->height = h;

    // Allocate buffer for RGB24 data (w * h * 3 bytes, no linesize padding for tightly packed).
    const int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGB24, w, h, 1);
    if (buffer_size <= 0) {
        av_frame_free(&rgb_frame);
        sws_freeContext(sws_rgb);
        return std::nullopt;
    }

    uint8_t* rgb_buffer = static_cast<uint8_t*>(av_malloc(buffer_size));
    if (!rgb_buffer) {
        av_frame_free(&rgb_frame);
        sws_freeContext(sws_rgb);
        return std::nullopt;
    }

    // Assign buffer to frame with tight packing.
    if (av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, rgb_buffer,
                             AV_PIX_FMT_RGB24, w, h, 1) < 0) {
        av_free(rgb_buffer);
        av_frame_free(&rgb_frame);
        sws_freeContext(sws_rgb);
        return std::nullopt;
    }

    // Convert (scale) the decoded frame to RGB24.
    const int slices = sws_scale(sws_rgb, frame_->data, frame_->linesize, 0, h,
                                rgb_frame->data, rgb_frame->linesize);
    if (slices != h) {
        av_free(rgb_buffer);
        av_frame_free(&rgb_frame);
        sws_freeContext(sws_rgb);
        return std::nullopt;
    }

    // Copy RGB data to ImageData. Since we allocated with tight packing, we can
    // memcpy the entire buffer.
    image::ImageData result;
    result.width  = w;
    result.height = h;
    result.format = image::ImageFormat::Unknown;
    result.pixels.resize(w * h * 3);
    std::memcpy(result.pixels.data(), rgb_buffer, result.pixels.size());

    // Clean up.
    av_free(rgb_buffer);
    av_frame_free(&rgb_frame);
    sws_freeContext(sws_rgb);

    return result;
}

} // namespace media

#endif // OSV_VENDORED_AV
