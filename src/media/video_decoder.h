#pragma once

#ifdef OSV_VENDORED_AV

#include <optional>
#include <cstdint>

#include "media/decoded_frame.h"
#include "vault/index.h"

extern "C" {
#include <libavformat/avio.h>
}

// Forward declarations for opaque FFmpeg pointers
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace media {

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    // Non-copyable (owns raw FFmpeg pointers)
    VideoDecoder(const VideoDecoder&)            = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // Movable
    VideoDecoder(VideoDecoder&&)            noexcept;
    VideoDecoder& operator=(VideoDecoder&&) noexcept;

    // Open a video stream from the given AVIO context (borrowed; not closed/freed by us).
    // Returns false on failure (e.g., unsupported codec, not a valid video).
    [[nodiscard]] bool open(AVIOContext* pb);

    // Decode and return the next frame, or std::nullopt at end/error.
    // Returned DecodedFrame's planes point into our owned frame (valid until next_frame/destruction).
    [[nodiscard]] std::optional<DecodedFrame> next_frame();

    // Accessors
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    uint64_t duration_us() const noexcept { return duration_us_; }
    vault::VideoCodec codec() const noexcept { return codec_; }

private:
    AVFormatContext* fmt_           = nullptr;
    AVCodecContext*  codec_ctx_     = nullptr;
    AVFrame*         frame_         = nullptr;
    AVPacket*        pkt_           = nullptr;
    int              stream_index_  = -1;
    int              width_         = 0;
    int              height_        = 0;
    uint64_t         duration_us_   = 0;
    vault::VideoCodec codec_        = vault::VideoCodec::Unknown;
    double           time_base_     = 0.0;
    bool             flushed_       = false;  // Track if we've sent the flush packet
};

} // namespace media

#endif // OSV_VENDORED_AV
