#pragma once

#ifdef OSV_VENDORED_AV

#include <optional>
#include <cstdint>
#include <string_view>
#include <deque>

#include "media/decoded_frame.h"
#include "media/audio_decoder.h"
#include "media/audio_frame.h"
#include "vault/index.h"
#include "image/image.h"

extern "C" {
#include <libavformat/avio.h>
#include <libswscale/swscale.h>
}

// Forward declarations for opaque FFmpeg pointers
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace media {

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    // Non-copyable and non-movable (owns raw FFmpeg pointers)
    // The destructor is user-declared, so move operations are implicitly deleted.
    VideoDecoder(const VideoDecoder&)            = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // Open a video stream from the given AVIO context (borrowed; not closed/freed by us).
    // Returns false on failure (e.g., unsupported codec, not a valid video).
    [[nodiscard]] bool open(AVIOContext* pb);

    // Decode and return the next frame, or std::nullopt at end/error.
    // Returned DecodedFrame's planes point into our owned frame (valid until next_frame/destruction).
    [[nodiscard]] std::optional<DecodedFrame> next_frame();

    // Seek to a timestamp (in seconds). Uses keyframe-anchored seeking and decodes forward
    // to land on the exact PTS. Returns false on failure.
    [[nodiscard]] bool seek(double ts_seconds);

    // Decode the first frame as RGB24 and return it as ImageData. Returns nullopt on failure.
    [[nodiscard]] std::optional<image::ImageData> decode_poster_rgb();

    // Audio support: returns true if an audio stream exists and is valid
    [[nodiscard]] bool has_audio() const noexcept { return audio_index_ >= 0 && audio_dec_.valid(); }

    // Audio metadata
    struct AudioInfo {
        int channels = 0;
        int sample_rate = 0;
    };
    [[nodiscard]] AudioInfo audio_info() const noexcept { return {audio_dec_.channels(), audio_dec_.sample_rate()}; }

    // Decode and return the next audio frame, or std::nullopt at end/error.
    [[nodiscard]] std::optional<AudioFrame> next_audio_frame();

    // Accessors
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    uint64_t duration_us() const noexcept { return duration_us_; }
    vault::VideoCodec codec() const noexcept { return codec_; }

private:
    // Helper: convert an AVFrame with unsupported format to I420 via swscale.
    std::optional<DecodedFrame> convert_to_i420(double pts_seconds);

    // Helper: read one packet and send it to the decoder, handling EOF/flush.
    bool pump_one_packet();

    // Helper: read one packet and route it to the appropriate stream queue (video or audio).
    // Returns false at EOF.
    bool read_and_route();

    // Helper: build a DecodedFrame from the just-received frame_ (swscale if needed).
    std::optional<DecodedFrame> build_from_current_frame(double pts_seconds);

    // Helper: drain one video packet from vq_, send to decoder, return false on send error.
    [[nodiscard]] bool drain_video_queue();

    // Helper: decode one audio packet from aq_, accumulate frames to audio_out_.
    void decode_audio_packet();

    // Free all owned FFmpeg resources (destructor + open() error paths).
    void reset();
    // Log msg, reset(), and return false — used at every open() failure site.
    bool fail_open(std::string_view msg);

    AVFormatContext* fmt_                = nullptr;
    AVCodecContext*  codec_ctx_          = nullptr;
    AVFrame*         frame_              = nullptr;
    AVPacket*        pkt_                = nullptr;
    SwsContext*      sws_                = nullptr;  // swscale context for format conversion
    AVFrame*         conv_               = nullptr;  // converted frame (for non-420p formats)
    int              stream_index_       = -1;
    int              width_              = 0;
    int              height_             = 0;
    uint64_t         duration_us_        = 0;
    vault::VideoCodec codec_             = vault::VideoCodec::Unknown;
    double           time_base_          = 0.0;
    AVRational       stream_time_base_   = {0, 1};  // raw stream time base for seek
    bool             flushed_            = false;   // Track if we've sent the flush packet
    double           pending_seek_target_ = -1.0;   // Target PTS for seek decode-forward

    // Audio support
    AudioDecoder                audio_dec_;           // audio codec context
    int                         audio_index_         = -1;  // audio stream index, or -1 if none
    std::deque<AVPacket*>       vq_;                 // queued video packets
    std::deque<AVPacket*>       aq_;                 // queued audio packets
    std::deque<AudioFrame>      audio_out_;          // decoded audio frames waiting to be returned
};

} // namespace media

#endif // OSV_VENDORED_AV
