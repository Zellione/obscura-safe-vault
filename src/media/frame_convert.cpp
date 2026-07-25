#ifdef OSV_VENDORED_AV

#include "media/frame_convert.h"

#include <array>
#include <cstring>
#include <format>
#include <string>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <print>

namespace media {

bool should_deinterlace(int frame_flags) noexcept
{
    // frame_flags is an AVFrame::flags value; the interlaced bit is the one
    // signal we act on. Takes an int (not the AVFrame) so callers/tests stay
    // decoupled from FFmpeg types, but this .cpp is OSV_VENDORED_AV-only, so we
    // reference the real macro rather than hardcoding its value.
    return (frame_flags & AV_FRAME_FLAG_INTERLACED) != 0;
}

FrameConverter::~FrameConverter()
{
    reset();
}

void FrameConverter::reset()
{
    if (sws_)  { sws_freeContext(sws_); sws_ = nullptr; }
    if (conv_) av_frame_free(&conv_);
    free_deint_graph();
}

void FrameConverter::free_deint_graph()
{
    if (filter_graph_) avfilter_graph_free(&filter_graph_);  // nulls filter_graph_
    if (deint_out_)    av_frame_free(&deint_out_);           // nulls deint_out_
    buffersrc_        = nullptr;                             // owned by the graph
    buffersink_       = nullptr;
    deint_width_      = 0;
    deint_height_     = 0;
    deint_format_     = -1;
    deint_error_once_ = false;
}

void FrameConverter::deint_warn_once(std::string_view msg)
{
    if (deint_error_once_) return;
    std::println(stderr, "[FrameConverter] {}", msg);
    deint_error_once_ = true;
}

bool FrameConverter::fail_deint_graph(std::string_view msg)
{
    deint_warn_once(msg);
    free_deint_graph();
    return false;
}

bool FrameConverter::build_deint_graph(const AVFrame* src)
{
    filter_graph_ = avfilter_graph_alloc();
    if (!filter_graph_) return fail_deint_graph("Failed to allocate filter graph");

    // buffer source args. time_base can be 1/1 since we carry pts separately.
    const std::string args =
        std::format("video_size={}x{}:pix_fmt={}:time_base=1/1:sar={}/{}",
                    src->width, src->height, src->format,
                    src->sample_aspect_ratio.num ? src->sample_aspect_ratio.num : 1,
                    src->sample_aspect_ratio.den ? src->sample_aspect_ratio.den : 1);

    AVFilterContext* buffersrc = nullptr;
    if (avfilter_graph_create_filter(&buffersrc, avfilter_get_by_name("buffer"), "in",
                                     args.c_str(), nullptr, filter_graph_) < 0) {
        return fail_deint_graph("Failed to create buffer filter");
    }

    // yadif with mode=0 (send_frame: one output frame per input frame).
    AVFilterContext* yadif = nullptr;
    if (avfilter_graph_create_filter(&yadif, avfilter_get_by_name("yadif"), "yadif",
                                     "mode=0", nullptr, filter_graph_) < 0) {
        return fail_deint_graph("Failed to create yadif filter");
    }

    AVFilterContext* buffersink = nullptr;
    if (avfilter_graph_create_filter(&buffersink, avfilter_get_by_name("buffersink"),
                                     "out", nullptr, nullptr, filter_graph_) < 0) {
        return fail_deint_graph("Failed to create buffersink filter");
    }

    // Link: buffersrc -> yadif -> buffersink
    if (avfilter_link(buffersrc, 0, yadif, 0) < 0) {
        return fail_deint_graph("Failed to link buffersrc to yadif");
    }
    if (avfilter_link(yadif, 0, buffersink, 0) < 0) {
        return fail_deint_graph("Failed to link yadif to buffersink");
    }
    if (avfilter_graph_config(filter_graph_, nullptr) < 0) {
        return fail_deint_graph("Failed to configure filter graph");
    }

    deint_out_ = av_frame_alloc();
    if (!deint_out_) return fail_deint_graph("Failed to allocate deinterlace output frame");

    // Only now commit the cached state — every path above tears the graph back
    // down, so a half-built graph is never left looking valid.
    buffersrc_    = buffersrc;
    buffersink_   = buffersink;
    deint_width_  = src->width;
    deint_height_ = src->height;
    deint_format_ = src->format;
    return true;
}

const AVFrame* FrameConverter::deinterlace(const AVFrame* src)
{
    if (!src) return nullptr;

    // Rebuild the graph on first use and whenever the source geometry changes.
    if (!filter_graph_ || deint_width_ != src->width || deint_height_ != src->height ||
        deint_format_ != src->format) {
        free_deint_graph();
        if (!build_deint_graph(src)) return nullptr;
    }

    // av_buffersrc_write_frame is the const, keep-a-reference form. The
    // non-const av_buffersrc_add_frame would take ownership of src's references
    // and blank the caller's frame — which the EAGAIN path below explicitly
    // relies on NOT happening (the caller reuses src for that frame).
    if (av_buffersrc_write_frame(buffersrc_, src) < 0) {
        deint_warn_once("Failed to add frame to buffersrc");
        return nullptr;
    }

    // Release the previous output's buffers first: av_buffersink_get_frame
    // move-refs into deint_out_ without unreferencing it, so skipping this
    // leaks one whole frame buffer per deinterlaced frame (same failure mode
    // as to_i420()'s av_frame_unref below).
    av_frame_unref(deint_out_);

    const int ret = av_buffersink_get_frame(buffersink_, deint_out_);
    if (ret == AVERROR(EAGAIN)) {
        // yadif needs the next frame before it can emit; return nullptr.
        // The caller will show the original src once instead of dropping.
        return nullptr;
    }
    if (ret < 0) {
        deint_warn_once(std::format("av_buffersink_get_frame failed: {}", ret));
        return nullptr;
    }

    return deint_out_;
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

DecodedFrame copy_owned_frame(const DecodedFrame& src, std::vector<uint8_t>& storage)
{
    const int chroma_h = (src.height + 1) / 2;
    const int plane_count = src.pix_fmt == FramePixelFormat::I420 ? 3 : 2;

    std::array<size_t, 3> plane_bytes{0, 0, 0};
    plane_bytes[0] = static_cast<size_t>(src.linesizes[0]) * static_cast<size_t>(src.height);
    if (plane_count >= 2)
        plane_bytes[1] = static_cast<size_t>(src.linesizes[1]) * static_cast<size_t>(chroma_h);
    if (plane_count == 3)
        plane_bytes[2] = static_cast<size_t>(src.linesizes[2]) * static_cast<size_t>(chroma_h);

    size_t total = plane_bytes[0] + plane_bytes[1] + plane_bytes[2];
    storage.resize(total);

    DecodedFrame out{};
    out.width       = src.width;
    out.height      = src.height;
    out.pix_fmt      = src.pix_fmt;
    out.pts_seconds = src.pts_seconds;
    out.linesizes    = src.linesizes;

    size_t offset = 0;
    for (int i = 0; i < plane_count; ++i) {
        if (plane_bytes[i] == 0 || !src.planes[i]) {
            out.planes[i] = nullptr;
            continue;
        }
        std::memcpy(storage.data() + offset, src.planes[i], plane_bytes[i]);
        out.planes[i] = storage.data() + offset;
        offset += plane_bytes[i];
    }
    if (plane_count < 3) out.planes[2] = nullptr;

    return out;
}

} // namespace media

#endif // OSV_VENDORED_AV
