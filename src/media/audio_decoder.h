#pragma once

#include "media/audio_frame.h"
#include <vector>

#ifdef OSV_VENDORED_AV

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace media {

// Decodes one audio stream's packets to interleaved-F32 AudioFrames. Borrows
// the demuxer's AVStream (does not own the AVFormatContext).
class AudioDecoder {
public:
    AudioDecoder() = default;
    ~AudioDecoder();

    // Non-copyable and non-movable
    AudioDecoder(const AudioDecoder&)            = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    [[nodiscard]] bool open(const AVStream* stream);
    // Send `pkt` (nullptr to flush/drain) and append any produced frames.
    void decode(AVPacket* pkt, std::vector<AudioFrame>& out);
    // Flush decoder state (on seek)
    void flush();

    [[nodiscard]] int  channels()    const noexcept { return channels_; }
    [[nodiscard]] int  sample_rate() const noexcept { return sample_rate_; }
    [[nodiscard]] bool valid()       const noexcept { return ctx_ != nullptr; }

private:
    void reset();

    AVCodecContext* ctx_         = nullptr;
    AVFrame*        frame_       = nullptr;
    int             channels_    = 0;
    int             sample_rate_ = 0;
    AVRational      time_base_   = {0, 1};
};

}  // namespace media

#endif  // OSV_VENDORED_AV
