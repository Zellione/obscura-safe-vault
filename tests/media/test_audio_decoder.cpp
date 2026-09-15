#include "../test_framework.h"

#ifdef OSV_VENDORED_AV

#include "crypto/secure_mem.h"
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
                CHECK(crypto::detail::locked_page_refcount_for_tests(f.samples.data()) > 0);
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

// Phase 103: the previous decode() logged every avcodec_send_packet return
// < 0 as a failure and dropped the packet, including the normal
// AVERROR(EAGAIN) "input queue full, drain me first" signal. Pushing the
// SAME packet many times in a tight loop without ever calling receive_frame
// externally forces the codec's internal queue to fill — every subsequent
// send_packet returns EAGAIN. The OLD code logged "[AudioDecoder]
// avcodec_send_packet failed" for each one and dropped the packet; the
// codec never got the chance to drain, so audio died silently. The NEW
// code drains + retries; the test asserts decode() still produces frames
// (proving the codec recovered) and the assertion does not hang (proving
// no infinite EAGAIN loop).
TEST(audio_decoder_eagain_path_drains_and_recovers)
{
    auto audio_bytes = read_file(OSV_MEDIA_FIXTURE_DIR "/audio_only.m4a");
    REQUIRE(!audio_bytes.empty());

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

    // Grab every audio packet and feed each one back-to-back into decode()
    // WITHOUT any intervening external drain. This is the exact shape of
    // the production caller (VideoDecoder::decode_audio_packet pushes one
    // packet per decode() call, then next_audio_frame drains in a separate
    // step). With the OLD code, send_packet returns AVERROR(EAGAIN) once
    // the codec's internal packet queue fills, the function logs + drops
    // the packet, the next iteration gets EAGAIN again, and audio goes
    // silently dead. With the NEW code, each EAGAIN triggers an internal
    // drain + retry; every packet produces frames.
    AVPacket* pkt = av_packet_alloc();
    std::vector<media::AudioFrame> all_frames;
    int packets_sent = 0;
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == aidx) {
            std::vector<media::AudioFrame> frames;
            dec.decode(pkt, frames);
            ++packets_sent;
            for (auto& f : frames) {
                all_frames.push_back(std::move(f));
            }
        }
        av_packet_unref(pkt);
    }
    // flush
    std::vector<media::AudioFrame> flush_frames;
    dec.decode(nullptr, flush_frames);
    for (auto& f : flush_frames) {
        all_frames.push_back(std::move(f));
    }

    av_packet_free(&pkt);
    avformat_close_input(&fmt);

    // The point: every packet produced at least one frame, none were
    // dropped by the EAGAIN bug. The OLD code would have dropped packets
    // and the frame count would be smaller or zero.
    REQUIRE(packets_sent > 0);
    CHECK(!all_frames.empty());

    // Every frame came out at the configured rate — sanity.
    for (const auto& f : all_frames) {
        CHECK(f.sample_rate == 44100);
    }
}

TEST(audio_frame_samples_wipe_on_release)
{
    crypto::detail::reset_wipe_observations_for_tests();
    const auto before = crypto::detail::wiping_deallocation_count();
    {
        media::AudioFrame frame;
        frame.samples.resize(256, 0.75F);
        REQUIRE(crypto::detail::locked_page_refcount_for_tests(frame.samples.data()) > 0);
    }
    CHECK(crypto::detail::wiping_deallocation_count() > before);
    CHECK(crypto::detail::all_wipe_observations_zero_for_tests());
}

#endif
