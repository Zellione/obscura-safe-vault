#ifdef OSV_VENDORED_AV

#include "media/audio_decoder.h"
#include "media/audio_interleave.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/samplefmt.h>
}

#include <print>

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
        std::println(stderr, "[AudioDecoder] stream is null");
        return false;
    }

    if (!stream->codecpar) {
        std::println(stderr, "[AudioDecoder] codecpar is null");
        return false;
    }

    // Find and open the audio decoder
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        std::println(stderr, "[AudioDecoder] No decoder found for codec id {}",
                    static_cast<int>(stream->codecpar->codec_id));
        return false;
    }

    ctx_ = avcodec_alloc_context3(codec);
    if (!ctx_) {
        std::println(stderr, "[AudioDecoder] Failed to allocate codec context");
        return false;
    }

    if (avcodec_parameters_to_context(ctx_, stream->codecpar) < 0) {
        std::println(stderr, "[AudioDecoder] avcodec_parameters_to_context failed");
        reset();
        return false;
    }

    if (avcodec_open2(ctx_, codec, nullptr) < 0) {
        std::println(stderr, "[AudioDecoder] avcodec_open2 failed");
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
        std::println(stderr, "[AudioDecoder] Failed to allocate frame");
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

    // Send packet (may be nullptr to flush/drain)
    if (avcodec_send_packet(ctx_, pkt) < 0) {
        std::println(stderr, "[AudioDecoder] avcodec_send_packet failed");
        return;
    }

    // Receive all available frames
    int ret;
    while ((ret = avcodec_receive_frame(ctx_, frame_)) == 0) {
        // Build AudioFrame from the decoded frame
        const int n = frame_->nb_samples;
        const int ch = channels_;

        AudioFrame af;
        af.channels = ch;
        af.sample_rate = sample_rate_;

        // Calculate PTS in seconds
        if (frame_->pts != AV_NOPTS_VALUE) {
            af.pts_seconds = static_cast<double>(frame_->pts) * av_q2d(time_base_);
        } else {
            af.pts_seconds = 0.0;
        }

        // Use pure interleave function to convert all sample formats to F32
        if (!interleave_to_f32(frame_->data, frame_->ch_layout.nb_channels, n, ch,
                               frame_->format, af.samples)) {
            std::println(stderr, "[AudioDecoder] Unsupported sample format: {}",
                        frame_->format);
            continue;
        }

        out.push_back(std::move(af));
    }

    if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        std::println(stderr, "[AudioDecoder] avcodec_receive_frame error: {}", ret);
    }
}


void AudioDecoder::flush()
{
    if (ctx_) {
        avcodec_flush_buffers(ctx_);
    }
}

}  // namespace media

#endif  // OSV_VENDORED_AV
