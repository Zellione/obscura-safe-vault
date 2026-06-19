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

#include <cstring>
#include <print>

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

bool VideoDecoder::open(AVIOContext* pb)
{
    if (!pb) {
        std::println(stderr, "[VideoDecoder] AVIO context is null");
        return false;
    }

    // Allocate format context and assign the I/O context
    fmt_ = avformat_alloc_context();
    if (!fmt_) {
        std::println(stderr, "[VideoDecoder] Failed to allocate format context");
        return false;
    }

    fmt_->pb = pb;

    // Open input (NULL url since pb provides the data)
    int ret = avformat_open_input(&fmt_, nullptr, nullptr, nullptr);
    if (ret < 0) {
        std::println(stderr, "[VideoDecoder] avformat_open_input failed: {}", ret);
        // avformat_open_input frees fmt_ on failure; null it
        fmt_ = nullptr;
        return false;
    }

    // Find stream info
    ret = avformat_find_stream_info(fmt_, nullptr);
    if (ret < 0) {
        std::println(stderr, "[VideoDecoder] avformat_find_stream_info failed: {}", ret);
        avformat_close_input(&fmt_);
        fmt_ = nullptr;
        return false;
    }

    // Find best video stream
    const AVCodec* decoder = nullptr;
    stream_index_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_VIDEO, -1, -1,
                                        &decoder, 0);
    if (stream_index_ < 0) {
        std::println(stderr, "[VideoDecoder] No video stream found");
        avformat_close_input(&fmt_);
        fmt_ = nullptr;
        return false;
    }

    if (!decoder) {
        std::println(stderr, "[VideoDecoder] No decoder found");
        avformat_close_input(&fmt_);
        fmt_ = nullptr;
        return false;
    }

    const AVStream* stream = fmt_->streams[stream_index_];
    if (!stream || !stream->codecpar) {
        std::println(stderr, "[VideoDecoder] Invalid stream or codecpar");
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
            std::println(stderr, "[VideoDecoder] Unsupported codec ID: {}",
                        static_cast<int>(stream->codecpar->codec_id));
            avformat_close_input(&fmt_);
            fmt_ = nullptr;
            return false;
    }

    // Allocate and fill codec context
    codec_ctx_ = avcodec_alloc_context3(decoder);
    if (!codec_ctx_) {
        std::println(stderr, "[VideoDecoder] Failed to allocate codec context");
        avformat_close_input(&fmt_);
        fmt_ = nullptr;
        return false;
    }

    ret = avcodec_parameters_to_context(codec_ctx_, stream->codecpar);
    if (ret < 0) {
        std::println(stderr, "[VideoDecoder] avcodec_parameters_to_context failed: {}", ret);
        avcodec_free_context(&codec_ctx_);
        avformat_close_input(&fmt_);
        fmt_ = nullptr;
        return false;
    }

    // Open the codec
    ret = avcodec_open2(codec_ctx_, decoder, nullptr);
    if (ret < 0) {
        std::println(stderr, "[VideoDecoder] avcodec_open2 failed: {}", ret);
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
        std::println(stderr, "[VideoDecoder] Failed to allocate frame");
        avcodec_free_context(&codec_ctx_);
        avformat_close_input(&fmt_);
        fmt_ = nullptr;
        return false;
    }

    pkt_ = av_packet_alloc();
    if (!pkt_) {
        std::println(stderr, "[VideoDecoder] Failed to allocate packet");
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
        std::println(stderr, "[VideoDecoder] Cannot seek: not opened");
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
        std::println(stderr, "[VideoDecoder] av_seek_frame failed: {}", ret);
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

// Helper: build a DecodedFrame from an AVFrame with I420 or NV12 pixel format (zero-copy).
static DecodedFrame build_frame_i420_or_nv12(AVFrame* frame, double pts_seconds)
{
    FramePixelFormat pix_fmt;
    int plane_count;

    if (frame->format == AV_PIX_FMT_YUV420P) {
        pix_fmt = FramePixelFormat::I420;
        plane_count = 3;
    } else {  // AV_PIX_FMT_NV12
        pix_fmt = FramePixelFormat::NV12;
        plane_count = 2;
    }

    DecodedFrame result{};
    result.width = frame->width;
    result.height = frame->height;
    result.pix_fmt = pix_fmt;
    result.pts_seconds = pts_seconds;

    for (int i = 0; i < plane_count; ++i) {
        result.planes[i] = frame->data[i];
        result.linesizes[i] = frame->linesize[i];
    }
    if (plane_count < 3) {
        result.planes[2] = nullptr;
        result.linesizes[2] = 0;
    }

    return result;
}

// Helper: convert an AVFrame with unsupported pixel format to I420 via swscale.
std::optional<DecodedFrame> VideoDecoder::convert_to_i420(double pts_seconds)
{
    // Lazy initialization of swscale context
    if (!sws_) {
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
            std::println(stderr, "[VideoDecoder] Failed to create swscale context");
            return std::nullopt;
        }
    }

    // Ensure conv_ is allocated with proper dimensions
    if (!conv_) {
        conv_ = av_frame_alloc();
        if (!conv_) {
            std::println(stderr, "[VideoDecoder] Failed to allocate conversion frame");
            return std::nullopt;
        }
    }

    // Release the previous frame's buffers before re-allocating. Without
    // this, av_frame_get_buffer overwrites conv_'s existing buffer refs and
    // leaks them — one whole frame buffer leaked per decoded frame.
    av_frame_unref(conv_);
    conv_->format = AV_PIX_FMT_YUV420P;
    conv_->width  = frame_->width;
    conv_->height = frame_->height;

    int buf_ret = av_frame_get_buffer(conv_, 0);
    if (buf_ret < 0) {
        std::println(stderr, "[VideoDecoder] av_frame_get_buffer failed: {}", buf_ret);
        return std::nullopt;
    }

    // Perform the scale/convert
    int ret = sws_scale(sws_,
                       frame_->data,
                       frame_->linesize,
                       0,
                       frame_->height,
                       conv_->data,
                       conv_->linesize);
    if (ret < 0) {
        std::println(stderr, "[VideoDecoder] sws_scale failed: {}", ret);
        return std::nullopt;
    }

    DecodedFrame result{};
    result.width = frame_->width;
    result.height = frame_->height;
    result.pix_fmt = FramePixelFormat::I420;
    result.pts_seconds = pts_seconds;
    for (int i = 0; i < 3; ++i) {
        result.planes[i] = conv_->data[i];
        result.linesizes[i] = conv_->linesize[i];
    }

    return result;
}

// Helper: read one packet and send it to the decoder, handling EOF/flush.
bool VideoDecoder::pump_one_packet()
{
    int ret = av_read_frame(fmt_, pkt_);
    if (ret == AVERROR_EOF) {
        // EOF: send flush packet
        flushed_ = true;
        avcodec_send_packet(codec_ctx_, nullptr);  // nullptr = flush
        return true;
    } else if (ret < 0) {
        // Read error — terminate gracefully
        return false;
    }

    // Only send our video stream packets
    if (pkt_->stream_index == stream_index_) {
        ret = avcodec_send_packet(codec_ctx_, pkt_);
        if (ret < 0) {
            // Decode error — terminate gracefully
            av_packet_unref(pkt_);
            return false;
        }
    }

    av_packet_unref(pkt_);
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
            // Got a frame; calculate PTS
            double pts_seconds = 0.0;
            if (frame_->best_effort_timestamp != AV_NOPTS_VALUE) {
                pts_seconds = static_cast<double>(frame_->best_effort_timestamp) * time_base_;
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
            switch (frame_->format) {
                case AV_PIX_FMT_YUV420P:
                case AV_PIX_FMT_NV12:
                    // Direct pass-through: already compatible
                    return build_frame_i420_or_nv12(frame_, pts_seconds);

                default:
                    // Unsupported format: convert via swscale
                    return convert_to_i420(pts_seconds);
            }
        } else if (ret == AVERROR(EAGAIN)) {
            // Decoder needs more input
            if (flushed_) {
                // Already sent flush packet and got EAGAIN again; end of stream
                return std::nullopt;
            }

            // Try to read a packet and send it
            if (!pump_one_packet()) {
                return std::nullopt;
            }
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
