#include "../test_framework.h"

#ifdef OSV_VENDORED_AV

#include "media/audio_decoder.h"
#include "media/mem_avio.h"

#include <fstream>

extern "C" {
#include <libavformat/avformat.h>
}

namespace {

// Read a file into a vector.
std::vector<uint8_t> read_file(const char* file_path)
{
    std::ifstream f(file_path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST(audio_decoder_decodes_aac_to_f32)
{
    // Read the audio fixture into memory
    auto audio_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/audio_only.m4a");
    REQUIRE(!audio_bytes.empty());

    // Open via custom AVIO (like VideoDecoder does)
    media::MemAvio avio(audio_bytes);
    AVFormatContext* fmt = avformat_alloc_context();
    REQUIRE(fmt != nullptr);
    fmt->pb = avio.ctx();

    int ret = avformat_open_input(&fmt, nullptr, nullptr, nullptr);
    REQUIRE(ret == 0);
    REQUIRE(avformat_find_stream_info(fmt, nullptr) >= 0);
    int aidx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    REQUIRE(aidx >= 0);

    media::AudioDecoder dec;
    REQUIRE(dec.open(fmt->streams[aidx]));
    REQUIRE(dec.channels() == 1);
    REQUIRE(dec.sample_rate() == 44100);

    AVPacket* pkt = av_packet_alloc();
    std::vector<media::AudioFrame> frames;
    size_t total_samples = 0;
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == aidx) {
            frames.clear();
            dec.decode(pkt, frames);
            for (auto& f : frames) {
                REQUIRE(f.channels == 1);
                REQUIRE(f.sample_rate == 44100);
                total_samples += f.samples.size();
            }
        }
        av_packet_unref(pkt);
    }
    // flush
    frames.clear();
    dec.decode(nullptr, frames);
    for (auto& f : frames) total_samples += f.samples.size();

    av_packet_free(&pkt);
    avformat_close_input(&fmt);

    // ~1s mono @ 44100 -> within a couple AAC frames (1024 samples) of 44100.
    REQUIRE(total_samples >= 40000);
    REQUIRE(total_samples <= 48000);
}

#endif
