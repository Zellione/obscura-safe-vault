#ifdef OSV_VENDORED_AV

#include "media/audio_decoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/samplefmt.h>
}

#include <cstring>
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

void AudioDecoder::decode(AVPacket* pkt, std::vector<AudioFrame>& out)
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
    while (true) {
        int ret = avcodec_receive_frame(ctx_, frame_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // No more frames available
            break;
        }
        if (ret < 0) {
            std::println(stderr, "[AudioDecoder] avcodec_receive_frame error: {}", ret);
            break;
        }

        // Build AudioFrame from the decoded frame
        int n = frame_->nb_samples;
        int ch = channels_;

        AudioFrame af;
        af.channels = ch;
        af.sample_rate = sample_rate_;

        // Calculate PTS in seconds
        if (frame_->pts != AV_NOPTS_VALUE) {
            af.pts_seconds = frame_->pts * av_q2d(time_base_);
        } else {
            af.pts_seconds = 0.0;
        }

        // Interleave samples to F32
        af.samples.resize(n * ch);

        // Handle different sample formats
        AVSampleFormat fmt = static_cast<AVSampleFormat>(frame_->format);

        switch (fmt) {
            case AV_SAMPLE_FMT_FLTP: {
                // Planar float32
                for (int s = 0; s < n; ++s) {
                    for (int c = 0; c < ch; ++c) {
                        af.samples[s * ch + c] =
                            reinterpret_cast<const float*>(frame_->data[c])[s];
                    }
                }
                break;
            }
            case AV_SAMPLE_FMT_FLT: {
                // Packed float32 - already interleaved
                const float* src = reinterpret_cast<const float*>(frame_->data[0]);
                std::memcpy(af.samples.data(), src, n * ch * sizeof(float));
                break;
            }
            case AV_SAMPLE_FMT_S16P: {
                // Planar int16 - convert to float
                for (int s = 0; s < n; ++s) {
                    for (int c = 0; c < ch; ++c) {
                        int16_t sample =
                            reinterpret_cast<const int16_t*>(frame_->data[c])[s];
                        af.samples[s * ch + c] = sample / 32768.0f;
                    }
                }
                break;
            }
            case AV_SAMPLE_FMT_S16: {
                // Packed int16
                const int16_t* src = reinterpret_cast<const int16_t*>(frame_->data[0]);
                for (int i = 0; i < n * ch; ++i) {
                    af.samples[i] = src[i] / 32768.0f;
                }
                break;
            }
            case AV_SAMPLE_FMT_S32P: {
                // Planar int32
                for (int s = 0; s < n; ++s) {
                    for (int c = 0; c < ch; ++c) {
                        int32_t sample =
                            reinterpret_cast<const int32_t*>(frame_->data[c])[s];
                        af.samples[s * ch + c] = sample / 2147483648.0f;
                    }
                }
                break;
            }
            case AV_SAMPLE_FMT_S32: {
                // Packed int32
                const int32_t* src = reinterpret_cast<const int32_t*>(frame_->data[0]);
                for (int i = 0; i < n * ch; ++i) {
                    af.samples[i] = src[i] / 2147483648.0f;
                }
                break;
            }
            case AV_SAMPLE_FMT_U8P: {
                // Planar uint8
                for (int s = 0; s < n; ++s) {
                    for (int c = 0; c < ch; ++c) {
                        uint8_t sample =
                            static_cast<const uint8_t*>(frame_->data[c])[s];
                        af.samples[s * ch + c] = (sample - 128) / 128.0f;
                    }
                }
                break;
            }
            case AV_SAMPLE_FMT_U8: {
                // Packed uint8
                const uint8_t* src = reinterpret_cast<const uint8_t*>(frame_->data[0]);
                for (int i = 0; i < n * ch; ++i) {
                    af.samples[i] = (src[i] - 128) / 128.0f;
                }
                break;
            }
            default: {
                std::println(stderr, "[AudioDecoder] Unsupported sample format: {}",
                            static_cast<int>(fmt));
                break;
            }
        }

        out.push_back(std::move(af));
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
