#include "ui/dup_video_sig.h"

#ifdef OSV_VENDORED_AV

#include <algorithm>
#include <array>
#include <cstdio>   // SEEK_SET/CUR/END
#include <vector>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace ui {

namespace {

constexpr int AVIO_BUF   = 1 << 16;   // 64 KiB, same as media::ChunkAvio
constexpr int HASH_SIDE  = 96;        // frames scale to 96x96 RGB24 before dHash
constexpr int SEEK_DECODE_CAP = 400;  // frames decoded per seek before giving up

// Stream state handed to the AVIO callbacks. Mirrors media::ChunkAvio (static
// member callbacks, void* opaque fixed by the FFmpeg C API), but over the
// caller's thread-safe byte reader instead of a VideoSource.
class Stream {
public:
    Stream(const DupStreamRead& read, uint64_t size) : read_(read), size_(size) {}

    static int read_cb(void* opaque, uint8_t* buf, int buf_size)
    {
        auto* s = static_cast<Stream*>(opaque);
        if (buf_size <= 0) return 0;
        const int64_t n = s->read_(s->pos_, std::span<uint8_t>(buf, static_cast<size_t>(buf_size)));
        if (n < 0)  return AVERROR(EIO);
        if (n == 0) return AVERROR_EOF;
        s->pos_ += static_cast<uint64_t>(n);
        return static_cast<int>(n);
    }

    static int64_t seek_cb(void* opaque, int64_t offset, int whence)
    {
        auto* s = static_cast<Stream*>(opaque);
        const auto size = static_cast<int64_t>(s->size_);
        if ((whence & AVSEEK_SIZE) == AVSEEK_SIZE) return size;
        whence &= ~AVSEEK_FORCE;

        int64_t new_pos = 0;
        switch (whence) {
            case SEEK_SET: new_pos = offset; break;
            case SEEK_CUR: new_pos = static_cast<int64_t>(s->pos_) + offset; break;
            case SEEK_END: new_pos = size + offset; break;
            default:       return AVERROR(EINVAL);
        }
        if (new_pos < 0) return AVERROR(EINVAL);
        s->pos_ = static_cast<uint64_t>(new_pos);
        return new_pos;
    }

private:
    const DupStreamRead& read_;
    uint64_t             size_;
    uint64_t             pos_ = 0;
};

// Everything FFmpeg-owned for one signature run, freed in reverse order.
struct SigCtx {
    SigCtx() = default;
    SigCtx(const SigCtx&)            = delete;   // owns FFmpeg resources
    SigCtx& operator=(const SigCtx&) = delete;
    SigCtx(SigCtx&&)                 = delete;
    SigCtx& operator=(SigCtx&&)      = delete;

    AVIOContext*     pb    = nullptr;
    AVFormatContext* fmt   = nullptr;
    AVCodecContext*  codec = nullptr;
    AVPacket*        pkt   = nullptr;
    AVFrame*         frame = nullptr;
    SwsContext*      sws   = nullptr;
    int              stream_index = -1;

    ~SigCtx()
    {
        if (sws) sws_freeContext(sws);
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (codec) avcodec_free_context(&codec);
        if (fmt) avformat_close_input(&fmt);
        if (pb) {
            av_freep(&pb->buffer);
            avio_context_free(&pb);
        }
    }
};

// Open container + best video stream with a SOFTWARE decoder (no hwaccel:
// single frames, determinism over speed).
bool open_ctx(Stream& stream, SigCtx& c)
{
    auto* buffer = static_cast<unsigned char*>(av_malloc(AVIO_BUF));
    if (!buffer) return false;
    c.pb = avio_alloc_context(buffer, AVIO_BUF, /*write_flag=*/0, &stream,
                              &Stream::read_cb, /*write=*/nullptr, &Stream::seek_cb);
    if (!c.pb) {
        av_free(buffer);
        return false;
    }

    c.fmt = avformat_alloc_context();
    if (!c.fmt) return false;
    c.fmt->pb = c.pb;
    // NULL url — pb supplies the data. On failure avformat_open_input frees fmt.
    if (avformat_open_input(&c.fmt, nullptr, nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(c.fmt, nullptr) < 0) return false;

    const AVCodec* dec = nullptr;
    c.stream_index = av_find_best_stream(c.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (c.stream_index < 0 || !dec) return false;

    c.codec = avcodec_alloc_context3(dec);
    if (!c.codec) return false;
    if (avcodec_parameters_to_context(c.codec, c.fmt->streams[c.stream_index]->codecpar) < 0)
        return false;
    if (avcodec_open2(c.codec, dec, nullptr) < 0) return false;

    c.pkt = av_packet_alloc();
    c.frame = av_frame_alloc();
    return c.pkt && c.frame;
}

// Feed the decoder the next packet of our stream; on demux EOF flush ONCE and
// let the caller go back to draining (av_packet_unref resets stream_index, so
// the post-EOF packet must never be sent). False = send error.
bool feed_decoder(SigCtx& c, bool& flushed)
{
    while (true) {
        av_packet_unref(c.pkt);
        if (av_read_frame(c.fmt, c.pkt) < 0) {
            flushed = true;
            return avcodec_send_packet(c.codec, nullptr) >= 0;
        }
        if (c.pkt->stream_index == c.stream_index)
            return avcodec_send_packet(c.codec, c.pkt) >= 0;
    }
}

// Decode forward from the current demux position until the first frame with
// pts >= target lands in c.frame. Bounded by SEEK_DECODE_CAP.
bool decode_until(SigCtx& c, int64_t target_pts)
{
    bool flushed = false;
    int decoded = 0;
    while (decoded < SEEK_DECODE_CAP) {
        const int ret = avcodec_receive_frame(c.codec, c.frame);
        if (ret == 0) {
            ++decoded;
            if (const int64_t pts = c.frame->best_effort_timestamp;
                pts == AV_NOPTS_VALUE || pts >= target_pts)
                return true;
            av_frame_unref(c.frame);
            continue;
        }
        if (ret != AVERROR(EAGAIN)) return false;   // drained (EOF) or hard error
        if (flushed) return false;                  // nothing more is coming
        if (!feed_decoder(c, flushed)) return false;
    }
    return false;
}

// Scale the decoded frame to HASH_SIDE^2 RGB24 and dHash it.
bool frame_to_hash(SigCtx& c, uint64_t& out_hash)
{
    c.sws = sws_getCachedContext(c.sws, c.frame->width, c.frame->height,
                                 static_cast<AVPixelFormat>(c.frame->format),
                                 HASH_SIDE, HASH_SIDE, AV_PIX_FMT_RGB24,
                                 SWS_AREA, nullptr, nullptr, nullptr);
    if (!c.sws) return false;

    std::vector<uint8_t> rgb(static_cast<size_t>(HASH_SIDE) * HASH_SIDE * 3);
    std::array<uint8_t*, 1>  dst{rgb.data()};
    const std::array<int, 1> dst_stride{HASH_SIDE * 3};
    const int scaled = sws_scale(c.sws, c.frame->data, c.frame->linesize, 0,
                                 c.frame->height, dst.data(), dst_stride.data());
    if (scaled <= 0) return false;

    out_hash = dhash64(std::span(rgb.data(), rgb.size()), HASH_SIDE, HASH_SIDE);
    return true;
}

} // namespace

bool compute_video_frame_sig(const DupStreamRead& read, uint64_t total_size, VideoSig& sig)
{
    Stream stream(read, total_size);
    SigCtx c;
    if (!open_ctx(stream, c)) return false;

    const AVStream* st = c.fmt->streams[c.stream_index];
    const int64_t duration = st->duration > 0
        ? st->duration
        : av_rescale_q(c.fmt->duration, AV_TIME_BASE_Q, st->time_base);
    if (duration <= 0) return false;

    for (size_t i = 0; i < DUP_VID_FRAME_POSITIONS.size(); ++i) {
        const auto target_pts = static_cast<int64_t>(
            static_cast<double>(duration) * DUP_VID_FRAME_POSITIONS[i]);
        if (av_seek_frame(c.fmt, c.stream_index, target_pts, AVSEEK_FLAG_BACKWARD) < 0)
            continue;                      // position stays invalid, try the next
        avcodec_flush_buffers(c.codec);

        if (uint64_t hash = 0; decode_until(c, target_pts) && frame_to_hash(c, hash)) {
            sig.frame_hash[i] = hash;
            sig.frame_valid |= 1u << i;
        }
        av_frame_unref(c.frame);
    }
    return true;
}

} // namespace ui

#else // !OSV_VENDORED_AV

namespace ui {

bool compute_video_frame_sig(const DupStreamRead& /*read*/, uint64_t /*total_size*/,
                             VideoSig& /*sig*/)
{
    return false;   // no FFmpeg in this build: the poster verdict decides
}

} // namespace ui

#endif // OSV_VENDORED_AV
