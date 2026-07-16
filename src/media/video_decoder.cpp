#ifdef OSV_VENDORED_AV

#include "media/video_decoder.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <cstring>
#include <print>

namespace media {

VideoDecoder::VideoDecoder() = default;

VideoDecoder::~VideoDecoder()
{
    reset();
}

// Free every FFmpeg resource we own. Each free nulls its own pointer except
// sws_, which we null explicitly. Shared by the destructor and open()'s error
// paths so the per-failure cleanup isn't duplicated at every call site.
void VideoDecoder::reset()
{
    if (codec_ctx_) avcodec_free_context(&codec_ctx_);
    if (fmt_)       avformat_close_input(&fmt_);   // frees fmt_ itself
    if (frame_)     av_frame_free(&frame_);
    if (pkt_)       av_packet_free(&pkt_);
    if (sws_)     { sws_freeContext(sws_); sws_ = nullptr; }
    if (conv_)      av_frame_free(&conv_);
    
    // Free queued packets and clear output buffers
    for (auto pkt : vq_) av_packet_free(&pkt);
    vq_.clear();
    for (auto pkt : aq_) av_packet_free(&pkt);
    aq_.clear();
    audio_out_.clear();
    audio_index_ = -1;
}

// Log an open() failure, release any partially-acquired state, and return false.
bool VideoDecoder::fail_open(std::string_view msg)
{
    std::println(stderr, "[VideoDecoder] {}", msg);
    reset();
    return false;
}

bool VideoDecoder::open(AVIOContext* pb)
{
    // Quiet FFmpeg's own chatty av_log: the "Protocol name not provided" notice on
    // our custom AVIO, and per-frame decode errors on corrupt input. Actionable
    // failures are surfaced via our own [VideoDecoder] logging. Global + idempotent.
    av_log_set_level(AV_LOG_FATAL);

    if (!pb) return fail_open("AVIO context is null");

    fmt_ = avformat_alloc_context();
    if (!fmt_) return fail_open("Failed to allocate format context");
    fmt_->pb = pb;

    // NULL url — pb supplies the data. On failure avformat_open_input frees fmt_
    // itself, so null it before fail_open()/reset() to avoid a double-free.
    if (avformat_open_input(&fmt_, nullptr, nullptr, nullptr) < 0) {
        fmt_ = nullptr;
        return fail_open("avformat_open_input failed");
    }
    if (avformat_find_stream_info(fmt_, nullptr) < 0) {
        return fail_open("avformat_find_stream_info failed");
    }

    const AVCodec* decoder = nullptr;
    stream_index_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (stream_index_ < 0 || !decoder) return fail_open("No video stream/decoder found");

    const AVStream* stream = fmt_->streams[stream_index_];
    if (!stream || !stream->codecpar) return fail_open("Invalid stream or codecpar");

    using enum vault::VideoCodec;
    switch (stream->codecpar->codec_id) {
        case AV_CODEC_ID_H264:   codec_ = H264;   break;
        case AV_CODEC_ID_HEVC:   codec_ = HEVC;   break;
        case AV_CODEC_ID_PRORES: codec_ = ProRes; break;  // Phase 28
        case AV_CODEC_ID_DNXHD:  codec_ = DNxHD;  break;  // Phase 28
        case AV_CODEC_ID_MJPEG:  codec_ = MJPEG;  break;  // Phase 28
        case AV_CODEC_ID_VP8:    codec_ = VP8;    break;  // Phase 38
        case AV_CODEC_ID_VP9:     codec_ = VP9;     break;  // Phase 38
        case AV_CODEC_ID_AV1:     codec_ = AV1;     break;  // Phase 40
        case AV_CODEC_ID_QTRLE:   codec_ = QTRLE;   break;  // Phase 40 (.mov)
        case AV_CODEC_ID_CINEPAK: codec_ = Cinepak; break;  // Phase 40 (.mov)
        default:                  return fail_open("Unsupported codec");
    }

    codec_ctx_ = avcodec_alloc_context3(decoder);
    if (!codec_ctx_) return fail_open("Failed to allocate codec context");
    if (avcodec_parameters_to_context(codec_ctx_, stream->codecpar) < 0) {
        return fail_open("avcodec_parameters_to_context failed");
    }
    if (avcodec_open2(codec_ctx_, decoder, nullptr) < 0) {
        return fail_open("avcodec_open2 failed");
    }

    width_            = codec_ctx_->width;
    height_           = codec_ctx_->height;
    time_base_        = av_q2d(stream->time_base);
    stream_time_base_ = stream->time_base;

    // Duration in microseconds: prefer fmt_->duration (AV_TIME_BASE units = µs).
    if (fmt_->duration > 0 && fmt_->duration != AV_NOPTS_VALUE) {
        duration_us_ = static_cast<uint64_t>(fmt_->duration);
    } else if (stream->duration > 0 && stream->duration != AV_NOPTS_VALUE) {
        duration_us_ = static_cast<uint64_t>(static_cast<double>(stream->duration)
                                             * av_q2d(stream->time_base) * 1e6);
    } else {
        duration_us_ = 0;
    }

    frame_ = av_frame_alloc();
    if (!frame_) return fail_open("Failed to allocate frame");
    pkt_ = av_packet_alloc();
    if (!pkt_) return fail_open("Failed to allocate packet");

    // Attempt to open audio stream (non-fatal if missing)
    audio_index_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_index_ >= 0) {
        const AVStream* audio_stream = fmt_->streams[audio_index_];
        if (audio_stream && !audio_dec_.open(audio_stream)) {
            std::println(stderr, "[VideoDecoder] Audio stream found but failed to open decoder; continuing video-only");
            audio_index_ = -1;
        }
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
    if (const int ret = av_seek_frame(fmt_, stream_index_, ts, AVSEEK_FLAG_BACKWARD); ret < 0) {
        std::println(stderr, "[VideoDecoder] av_seek_frame failed: {}", ret);
        return false;
    }

    // Flush the codecs so they discard any buffered frames
    avcodec_flush_buffers(codec_ctx_);
    audio_dec_.flush();

    // Clear all packet and audio output queues (they belong to the old seek position)
    for (auto pkt : vq_) av_packet_free(&pkt);
    vq_.clear();
    for (auto pkt : aq_) av_packet_free(&pkt);
    aq_.clear();
    audio_out_.clear();

    // Reset decode state
    flushed_ = false;

    // Set the pending seek target so next_frame() will decode forward until reaching it
    pending_seek_target_ = ts_seconds;

    return true;
}

// Helper: build a DecodedFrame from an AVFrame with I420 or NV12 pixel format (zero-copy).
static DecodedFrame build_frame_i420_or_nv12(const AVFrame* frame, double pts_seconds)
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

    if (int buf_ret = av_frame_get_buffer(conv_, 0); buf_ret < 0) {
        std::println(stderr, "[VideoDecoder] av_frame_get_buffer failed: {}", buf_ret);
        return std::nullopt;
    }

    // Perform the scale/convert
    if (int ret = sws_scale(sws_,
                            frame_->data,
                            frame_->linesize,
                            0,
                            frame_->height,
                            conv_->data,
                            conv_->linesize);
        ret < 0) {
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

// Helper: read one packet and route it to the appropriate stream queue.
// Returns false at EOF.
bool VideoDecoder::read_and_route()
{
    if (int ret = av_read_frame(fmt_, pkt_); ret == AVERROR_EOF || ret < 0) {
        return false;
    }

    // Route packet to the appropriate stream queue
    if (pkt_->stream_index == stream_index_) {
        // Video packet: clone and queue
        AVPacket* cloned = av_packet_clone(pkt_);
        if (cloned) {
            vq_.push_back(cloned);
        } else {
            std::println(stderr, "[VideoDecoder] packet clone failed (out of memory); dropping packet");
        }
    } else if (audio_index_ >= 0 && pkt_->stream_index == audio_index_) {
        // Audio packet: clone and queue
        AVPacket* cloned = av_packet_clone(pkt_);
        if (cloned) {
            aq_.push_back(cloned);
        } else {
            std::println(stderr, "[VideoDecoder] packet clone failed (out of memory); dropping packet");
        }
    }
    // Else: ignore packets from other streams

    av_packet_unref(pkt_);
    return true;
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

// Helper: turn the just-received frame_ into a DecodedFrame, converting via
// swscale when the pixel format isn't directly uploadable.
std::optional<DecodedFrame> VideoDecoder::build_from_current_frame(double pts_seconds)
{
    if (frame_->format == AV_PIX_FMT_YUV420P || frame_->format == AV_PIX_FMT_NV12) {
        return build_frame_i420_or_nv12(frame_, pts_seconds);  // zero-copy
    }
    return convert_to_i420(pts_seconds);                       // swscale fallback
}

// Helper: drain one video packet from vq_ and send to decoder.
// Returns false if send fails; true otherwise (sent or queue empty).
bool VideoDecoder::drain_video_queue()
{
    if (vq_.empty()) {
        return true;
    }

    AVPacket* pkt = vq_.front();
    vq_.pop_front();
    const int send_ret = avcodec_send_packet(codec_ctx_, pkt);
    av_packet_free(&pkt);
    return send_ret >= 0;
}

// Helper: decode one audio packet from aq_, accumulate frames to audio_out_.
void VideoDecoder::decode_audio_packet()
{
    if (aq_.empty()) {
        return;
    }

    AVPacket* pkt = aq_.front();
    aq_.pop_front();
    std::vector<AudioFrame> frames;
    audio_dec_.decode(pkt, frames);
    av_packet_free(&pkt);
    for (auto& f : frames) {
        audio_out_.push_back(std::move(f));
    }
}

// Helper: handle EAGAIN case (feed more packets or return nullptr).
// Returns true to continue the main loop, false to return nullptr.
bool VideoDecoder::handle_eagain_case()
{
    if (!drain_video_queue()) {
        return false;  // send failed; caller returns nullptr
    }
    if (!vq_.empty()) {
        return true;  // We just fed a queued packet; retry receive
    }

    // No queued packets: read and route from demuxer
    if (!read_and_route()) {
        // EOF: send flush packet
        flushed_ = true;
        avcodec_send_packet(codec_ctx_, nullptr);
        return true;  // Continue to retry receive
    }
    // After routing, try to send any new video packet
    return drain_video_queue();  // true if sent/queue empty, false if send failed
}

std::optional<DecodedFrame> VideoDecoder::next_frame()
{
    if (!codec_ctx_ || !frame_ || !pkt_) {
        return std::nullopt;
    }

    while (true) {
        const int ret = avcodec_receive_frame(codec_ctx_, frame_);
        if (ret == 0) {
            double pts_seconds = 0.0;
            if (frame_->best_effort_timestamp != AV_NOPTS_VALUE) {
                pts_seconds = static_cast<double>(frame_->best_effort_timestamp) * time_base_;
            }
            // Decode-forward past frames before a pending seek target.
            if (pending_seek_target_ >= 0.0 && pts_seconds < pending_seek_target_) {
                continue;
            }
            pending_seek_target_ = -1.0;  // reached the target (or none pending)
            return build_from_current_frame(pts_seconds);
        }

        // EAGAIN before flush: feed one more packet
        if (ret == AVERROR(EAGAIN) && !flushed_) {
            if (handle_eagain_case()) {
                continue;  // Continue the loop to retry receive
            }
            return std::nullopt;  // send failed
        }

        // EAGAIN after flush, EOF, a read/decode error, or a failed pump → done.
        return std::nullopt;
    }
}

std::optional<AudioFrame> VideoDecoder::next_audio_frame()
{
    // No audio stream
    if (audio_index_ < 0) {
        return std::nullopt;
    }

    // Return buffered decoded frame if available
    if (!audio_out_.empty()) {
        AudioFrame frame = std::move(audio_out_.front());
        audio_out_.pop_front();
        return frame;
    }

    // Drain queued audio packets
    while (!aq_.empty()) {
        decode_audio_packet();
    }

    // If we now have frames, return the first
    if (!audio_out_.empty()) {
        AudioFrame frame = std::move(audio_out_.front());
        audio_out_.pop_front();
        return frame;
    }

    // Read from demuxer until we get an audio packet or EOF
    bool eof = false;
    while (aq_.empty()) {
        if (!read_and_route()) {
            eof = true;
            break;
        }
    }

    // Process any audio packet we got
    decode_audio_packet();

    // On EOF, send a final flush to drain buffered frames
    if (eof && audio_out_.empty()) {
        std::vector<AudioFrame> frames;
        audio_dec_.decode(nullptr, frames);
        for (auto& f : frames) {
            audio_out_.push_back(std::move(f));
        }
    }

    // Return the first decoded frame if available
    if (!audio_out_.empty()) {
        AudioFrame frame = std::move(audio_out_.front());
        audio_out_.pop_front();
        return frame;
    }

    return std::nullopt;
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
    if (auto decoded = next_frame(); !decoded) {
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
    av_opt_set_int(sws_rgb, "src_format", frame_->format, 0);
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

    auto* rgb_buffer = static_cast<uint8_t*>(av_malloc(buffer_size));
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
    if (const int slices = sws_scale(sws_rgb, frame_->data, frame_->linesize, 0, h,
                                     rgb_frame->data, rgb_frame->linesize);
        slices != h) {
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
