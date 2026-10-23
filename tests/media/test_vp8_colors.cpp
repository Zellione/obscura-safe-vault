#include "test_framework.h"

#ifdef OSV_VENDORED_AV
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>
#include <vector>

#include "media/mem_avio.h"
#include "media/video_decoder.h"
#include "media/video_decode_worker.h"
#include "media/hwaccel_setting.h"
#include "media/hw_accel.h"

namespace {
struct DecodePreference {
    bool hardware = media::enable_hardware_decode();
    bool software = media::force_software_decode();
    ~DecodePreference()
    {
        media::set_enable_hardware_decode(hardware);
        media::set_force_software_decode(software);
        media::test_only_force_hwaccel_unavailable(false);
    }
};

std::vector<uint8_t> vp8_fixture()
{
    std::ifstream file(OSV_MEDIA_FIXTURE_DIR "/vp8_colors_odd.webm", std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void check_colors(const media::DecodedFrame& frame, int& _failures)
{
    REQUIRE(frame.width == 65);
    REQUIRE(frame.height == 49);
    REQUIRE(frame.pix_fmt == media::FramePixelFormat::I420 ||
            frame.pix_fmt == media::FramePixelFormat::NV12);
    constexpr std::array<std::array<int, 3>, 3> colors{{{81, 90, 240}, {145, 54, 34}, {41, 240, 110}}};
    const bool reversed = frame.pts_seconds >= 0.999;
    for (int plane = 0; plane < 3; ++plane) {
        const bool interleaved = plane != 0 && frame.pix_fmt == media::FramePixelFormat::NV12;
        const int storage_plane = interleaved ? 1 : plane;
        REQUIRE(frame.planes[storage_plane] != nullptr);
        const int height = plane == 0 ? 49 : 25;
        REQUIRE(frame.linesizes[storage_plane] >= (plane == 0 ? 65 : interleaved ? 66 : 33));
        for (int bar = 0; bar < 3; ++bar) {
            const int x = plane == 0 ? 10 + 22 * bar : 5 + 11 * bar;
            for (int y : {0, height / 2, height - 1}) {
                const int column = interleaved ? 2 * x + plane - 1 : x;
                const int actual = frame.planes[storage_plane][y * frame.linesizes[storage_plane] + column];
                CHECK(std::abs(actual - colors[reversed ? 2 - bar : bar][plane]) <= 6);
            }
        }
    }
}

void drain_colors(media::VideoDecodeWorker& worker, uint64_t generation, int expected, int& _failures)
{
    // Retain every result until after decoding finishes: later decode/unref must
    // not wipe or overwrite the pixels already handed to the rendering thread.
    std::vector<media::VideoDecodeWorker::Result> results;
    bool eof = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((!eof || worker.outstanding() != 0) && std::chrono::steady_clock::now() < deadline) {
        while (auto result = worker.take_result()) {
            REQUIRE(result->generation == generation);
            if (result->eof) eof = true;
            else results.push_back(std::move(*result));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(eof);
    REQUIRE(static_cast<int>(results.size()) == expected);
    double previous = -1;
    for (const auto& result : results) {
        REQUIRE(result.frame.has_value());
        CHECK(result.frame->pts_seconds > previous);
        previous = result.frame->pts_seconds;
        check_colors(*result.frame, _failures);
    }
}
} // namespace

TEST(vp8_colors_software_seek_reopen_and_poster)
{
    auto bytes = vp8_fixture();
    REQUIRE(!bytes.empty());
    for (int reopen = 0; reopen < 2; ++reopen) {
        media::MemAvio avio(bytes);
        media::VideoDecoder decoder;
        REQUIRE(decoder.open(avio.ctx()));
        CHECK(decoder.codec() == vault::VideoCodec::VP8);
        int count = 0;
        while (auto frame = decoder.next_frame()) {
            check_colors(*frame, _failures);
            ++count;
        }
        CHECK(count == 12);
        for (double target : {1.0, 0.0, 1.5}) {
            REQUIRE(decoder.seek(target));
            auto frame = decoder.next_frame();
            REQUIRE(frame.has_value());
            CHECK(std::abs(frame->pts_seconds - target) < 0.01);
            check_colors(*frame, _failures);
        }
        REQUIRE(decoder.seek(0));
        auto poster = decoder.decode_poster_rgb();
        REQUIRE(poster.has_value());
        REQUIRE(poster->width == 65);
        REQUIRE(poster->height == 49);
        for (int bar = 0; bar < 3; ++bar) {
            const size_t offset = static_cast<size_t>(24 * 65 + 10 + 22 * bar) * 3;
            for (int channel = 0; channel < 3; ++channel) {
                const int value = poster->pixels.data()[offset + static_cast<size_t>(channel)];
                CHECK(std::abs(value - (channel == bar ? 255 : 0)) <= 12);
            }
        }
    }
}

TEST(vp8_colors_worker_software_preference_fallback_and_hardware_probe)
{
    DecodePreference restore;
    auto bytes = vp8_fixture();
    REQUIRE(!bytes.empty());
    // F2 software, forced-unavailable fallback, then real probing (hardware only
    // on equipped runners). NV12 download and I420 software share the oracle.
    for (int mode = 0; mode < 3; ++mode) {
        media::set_enable_hardware_decode(mode != 0);
        media::set_force_software_decode(false);
        media::test_only_force_hwaccel_unavailable(mode == 1);
        media::MemAvio avio(bytes);
        media::VideoDecoder decoder;
        REQUIRE(decoder.open(avio.ctx()));
        media::VideoDecodeWorker worker(*decoder.video_codecpar(), decoder.video_time_base(), 0);
        for (uint64_t generation = 0; generation < 3; ++generation) {
            const double target = generation == 1 ? 1.0 : 0.0;
            if (generation != 0) {
                REQUIRE(decoder.seek_demux_only(target));
                worker.begin_seek(target);
            }
            while (AVPacket* packet = decoder.demux_next_video_packet()) worker.submit(packet, generation);
            worker.submit(nullptr, generation);
            drain_colors(worker, generation, generation == 1 ? 6 : 12, _failures);
        }
    }
}
#endif
