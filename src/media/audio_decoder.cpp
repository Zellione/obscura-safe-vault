#ifdef OSV_VENDORED_AV

#include "media/audio_decoder.h"
#include "media/audio_interleave.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/samplefmt.h>
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "media/ffmpeg_secure.h"
#include "platform/safe_print.h"

namespace media {

AudioDecoder::~AudioDecoder()
{
    reset();
}

void AudioDecoder::reset()
{
    if (ctx_) {
        avcodec_free_context(&ctx_);
    }
    if (frame_) {
        av_frame_free(&frame_);
    }
}

bool AudioDecoder::open(const AVStream* stream)
{
    if (!stream) {
        platform::safe_println(stderr, "[AudioDecoder] stream is null");
        return false;
    }

    if (!stream->codecpar) {
        platform::safe_println(stderr, "[AudioDecoder] codecpar is null");
        return false;
    }

    // Find and open the audio decoder
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        platform::safe_println(stderr, "[AudioDecoder] No decoder found for codec id {}",
                    static_cast<int>(stream->codecpar->codec_id));
        return false;
    }

    ctx_ = avcodec_alloc_context3(codec);
    if (!ctx_) {
        platform::safe_println(stderr, "[AudioDecoder] Failed to allocate codec context");
        return false;
    }

    if (avcodec_parameters_to_context(ctx_, stream->codecpar) < 0) {
        platform::safe_println(stderr, "[AudioDecoder] avcodec_parameters_to_context failed");
        reset();
        return false;
    }

    ctx_->get_buffer2 = &secure_get_buffer2;
    mark_ffmpeg_opaque_storage();

    if (avcodec_open2(ctx_, codec, nullptr) < 0) {
        platform::safe_println(stderr, "[AudioDecoder] avcodec_open2 failed");
        reset();
        return false;
    }

    // Cache channel count and sample rate
    channels_ = ctx_->ch_layout.nb_channels;
    sample_rate_ = ctx_->sample_rate;
    time_base_ = stream->time_base;

    // Allocate frame buffer
    frame_ = av_frame_alloc();
    if (!frame_) {
        platform::safe_println(stderr, "[AudioDecoder] Failed to allocate frame");
        reset();
        return false;
    }

    return true;
}

void AudioDecoder::decode(const AVPacket* pkt, std::vector<AudioFrame>& out)
{
    if (!ctx_ || !frame_) {
        return;
    }

    // Phase 103 fix: drain every ready frame into `out`. Logs (once per call,
    // not per frame) on a non-EAGAIN/non-EOF receive error. Used both BEFORE
    // and AFTER send_packet — see comments inline.
    //
    // Phase 103 also fixes a long-standing bug: avcodec_send_packet returns
    // AVERROR(EAGAIN) when the codec's input queue is full — the documented
    // "drain me with avcodec_receive_frame first, then retry" signal. The
    // OLD code treated EAGAIN as a hard error, logged "[AudioDecoder]
    // avcodec_send_packet failed", and DROPPED the packet — every subsequent
    // packet also got EAGAIN (queue never drained), so audio went silently
    // dead. The fix drains first, then sends; on EAGAIN it drains + retries.
    auto drain = [&](const char* context) {
        int ret;
        while ((ret = avcodec_receive_frame(ctx_, frame_)) == 0) {
            const int n = frame_->nb_samples;
            const int ch = channels_;

            AudioFrame af;
            af.channels = ch;
            af.sample_rate = sample_rate_;

            if (frame_->pts != AV_NOPTS_VALUE) {
                af.pts_seconds = static_cast<double>(frame_->pts) * av_q2d(time_base_);
            } else {
                af.pts_seconds = 0.0;
            }

            // Use pure interleave function to convert all sample formats to F32
            if (!interleave_to_f32(frame_->data, frame_->ch_layout.nb_channels, n, ch,
                                   frame_->format, af.samples)) {
                platform::safe_println(stderr, "[AudioDecoder] Unsupported sample format: {}",
                            frame_->format);
                continue;
            }

            out.push_back(std::move(af));
        }
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            // Real error during drain (not the normal "no more frames"
            // signal). Log the actual code + av_err2str so a future bug
            // here is debuggable instead of a generic wall of "failed".
            platform::safe_println(stderr, "[AudioDecoder] receive_frame {} error: {} ({})",
                        context, ret, av_err2str(ret));
        }
    };

    // Always drain whatever the codec has ready BEFORE we send the next
    // packet. The production caller (VideoDecoder::decode_audio_packet)
    // pushes one packet per decode() call with no external receive between
    // calls, so the codec's internal packet queue accumulates — without
    // this drain, send_packet would return AVERROR(EAGAIN) and (under the
    // OLD code) the packet would be silently dropped.
    drain("pre-send");

    // Send the packet (nullptr = drain/flush the decoder).
    // AVERROR(EAGAIN) means the codec's input queue is full — drain, retry.
    // Cap the retries so a pathological codec can't loop forever; if the
    // cap is hit, fall through to the failure branch which logs + returns.
    constexpr int MAX_SEND_RETRIES = 8;
    int send_ret = avcodec_send_packet(ctx_, pkt);
    for (int attempt = 0; send_ret == AVERROR(EAGAIN) && attempt < MAX_SEND_RETRIES; ++attempt) {
        drain("mid-send retry");
        send_ret = avcodec_send_packet(ctx_, pkt);
    }

    if (send_ret < 0) {
        // Genuine failure (not EAGAIN, or EAGAIN past the retry cap). Log
        // the actual AVERROR + av_err2str — under the OLD code this branch
        // also fired for the normal EAGAIN case, swamping stderr with
        // ~hundreds of "failed" lines per video and dropping the packets.
        platform::safe_println(stderr,
            "[AudioDecoder] avcodec_send_packet failed: {} ({})",
            send_ret, av_err2str(send_ret));
        return;
    }

    // Send succeeded — collect any frames the decoder produced.
    drain("post-send");
}


void AudioDecoder::flush()
{
    if (ctx_) {
        avcodec_flush_buffers(ctx_);
    }
}

}  // namespace media

#endif  // OSV_VENDORED_AV
