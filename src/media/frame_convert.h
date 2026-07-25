#pragma once

#ifdef OSV_VENDORED_AV

#include <cstdint>
#include <optional>
#include <vector>

#include "media/decoded_frame.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
extern "C" {
#include <libswscale/swscale.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

struct AVFrame;
struct AVFilterGraph;
struct AVFilterContext;

namespace media {

// True when a decoded frame carries interlaced content and should be deinterlaced.
[[nodiscard]] bool should_deinterlace(int frame_flags) noexcept;

// Converts a decoded AVFrame to a DecodedFrame, zero-copy for I420/NV12 and
// via a cached swscale context for anything else (e.g. yuv422p10le ProRes,
// yuvj422p MJPEG). One instance's swscale context is reused across frames of
// the same source (sws_getCachedContext no-ops when dimensions/format are
// unchanged) — construct one per decode stream, not per frame.
class FrameConverter {
public:
    FrameConverter() = default;
    ~FrameConverter();

    FrameConverter(const FrameConverter&)            = delete;
    FrameConverter& operator=(const FrameConverter&) = delete;

    // Zero-copy: valid only while `src` is unmodified (caller's AVFrame owns
    // the planes). Use for AV_PIX_FMT_YUV420P / AV_PIX_FMT_NV12 frames.
    [[nodiscard]] static DecodedFrame zero_copy(const AVFrame* src, double pts_seconds);

    // Converts `src` to I420 via swscale into this instance's owned `conv_`
    // frame. The returned DecodedFrame's planes are valid until the next
    // call to to_i420() or this object's destruction.
    [[nodiscard]] std::optional<DecodedFrame> to_i420(const AVFrame* src, double pts_seconds);

    // Frees the cached swscale context + conversion frame (same effect as
    // destruction, but leaves the object usable — to_i420() lazily
    // recreates both on the next call). FrameConverter has no move
    // operations (deleted copy + a user-declared destructor suppress the
    // implicit ones), so callers that need to "clear" a member use this
    // instead of reassigning from a temporary.
    void reset();

    // Deinterlaces an interlaced frame using yadif filter (mode=0: send_frame).
    // Returns a deinterlaced AVFrame (owned by this object, valid until the
    // next call) or nullptr if yadif needs more frames (EAGAIN on first frame)
    // or an error occurred. The caller should show the original src once in the
    // EAGAIN case rather than dropping a frame.
    [[nodiscard]] const AVFrame* deinterlace(const AVFrame* src);

private:
    SwsContext* sws_  = nullptr;
    AVFrame*    conv_ = nullptr;

    // yadif filter graph state (lazy-built, cached, rebuilt on format change)
    AVFilterGraph*  filter_graph_      = nullptr;
    AVFilterContext* buffersrc_        = nullptr;
    AVFilterContext* buffersink_       = nullptr;
    AVFrame*        deint_out_         = nullptr;
    int             deint_width_       = 0;
    int             deint_height_      = 0;
    int             deint_format_      = -1;
    bool            deint_error_once_  = false;
};

// Copies `src`'s plane data into `storage` (resized to fit) and returns a
// DecodedFrame whose planes/linesizes point into `storage`. `storage` must
// outlive the returned DecodedFrame. Chroma-plane row counts use
// (height + 1) / 2 for I420/NV12, matching FFmpeg's own subsampling
// rounding for odd dimensions.
[[nodiscard]] DecodedFrame copy_owned_frame(const DecodedFrame& src, std::vector<uint8_t>& storage);

} // namespace media

#endif // OSV_VENDORED_AV
