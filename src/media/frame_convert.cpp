#ifdef OSV_VENDORED_AV

#include "media/frame_convert.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <print>

namespace media {

FrameConverter::~FrameConverter()
{
    reset();
}

void FrameConverter::reset()
{
    if (sws_)  { sws_freeContext(sws_); sws_ = nullptr; }
    if (conv_) av_frame_free(&conv_);
}

DecodedFrame FrameConverter::zero_copy(const AVFrame* src, double pts_seconds)
{
    FramePixelFormat pix_fmt;
    int plane_count;
    if (src->format == AV_PIX_FMT_YUV420P) {
        pix_fmt = FramePixelFormat::I420;
        plane_count = 3;
    } else {  // AV_PIX_FMT_NV12
        pix_fmt = FramePixelFormat::NV12;
        plane_count = 2;
    }

    DecodedFrame result{};
    result.width       = src->width;
    result.height      = src->height;
    result.pix_fmt      = pix_fmt;
    result.pts_seconds = pts_seconds;
    for (int i = 0; i < plane_count; ++i) {
        result.planes[i]     = src->data[i];
        result.linesizes[i]  = src->linesize[i];
    }
    if (plane_count < 3) {
        result.planes[2]    = nullptr;
        result.linesizes[2] = 0;
    }
    return result;
}

std::optional<DecodedFrame> FrameConverter::to_i420(const AVFrame* src, double pts_seconds)
{
    if (!sws_) {
        sws_ = sws_getCachedContext(sws_, src->width, src->height,
                                    static_cast<AVPixelFormat>(src->format),
                                    src->width, src->height, AV_PIX_FMT_YUV420P,
                                    SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_) {
            std::println(stderr, "[FrameConverter] Failed to create swscale context");
            return std::nullopt;
        }
    }
    if (!conv_) {
        conv_ = av_frame_alloc();
        if (!conv_) {
            std::println(stderr, "[FrameConverter] Failed to allocate conversion frame");
            return std::nullopt;
        }
    }

    // Release the previous frame's buffers before re-allocating — without
    // this, av_frame_get_buffer overwrites conv_'s existing buffer refs and
    // leaks them, one whole frame buffer leaked per decoded frame.
    av_frame_unref(conv_);
    conv_->format = AV_PIX_FMT_YUV420P;
    conv_->width  = src->width;
    conv_->height = src->height;
    if (int buf_ret = av_frame_get_buffer(conv_, 0); buf_ret < 0) {
        std::println(stderr, "[FrameConverter] av_frame_get_buffer failed: {}", buf_ret);
        return std::nullopt;
    }

    if (int ret = sws_scale(sws_, src->data, src->linesize, 0, src->height,
                            conv_->data, conv_->linesize);
        ret < 0) {
        std::println(stderr, "[FrameConverter] sws_scale failed: {}", ret);
        return std::nullopt;
    }

    DecodedFrame result{};
    result.width       = src->width;
    result.height      = src->height;
    result.pix_fmt      = FramePixelFormat::I420;
    result.pts_seconds = pts_seconds;
    for (int i = 0; i < 3; ++i) {
        result.planes[i]    = conv_->data[i];
        result.linesizes[i] = conv_->linesize[i];
    }
    return result;
}

} // namespace media

#endif // OSV_VENDORED_AV
