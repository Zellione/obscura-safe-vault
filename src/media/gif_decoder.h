#pragma once

#ifdef OSV_VENDORED_AV

#include <memory>

// Streaming animated-GIF frame decoder (Phase 47).
//
// MemAvio over a plaintext in-memory buffer -> FFmpeg's gif demuxer -> gif
// decoder -> swscale to RGBA, which is what the existing image texture pipeline
// wants (no YUV texture path is involved).
//
// Deliberately NOT VideoDecoder: no audio, no packet queues, no seeking, no
// hardware acceleration. One frame at a time, so memory stays constant no
// matter how long the GIF is. FFmpeg's gif decoder performs frame disposal and
// composition internally, so every emitted frame is a complete image.
//
// Lifetime: open() borrows the caller's buffer (an mlock'd SecureBytes in
// production) and does not copy it. Keep it alive until the decoder is
// destroyed.

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace media {

struct GifFrame {
    std::vector<uint8_t> rgba;      // width*height*4
    int    width   = 0;
    int    height  = 0;
    double delay_s = 0.0;           // >= kMinFrameDelay
};

// GIFs commonly declare a 0 or 10 ms delay; every browser clamps to 20 ms.
inline constexpr double kMinFrameDelay = 0.02;

class GifDecoder {
public:
    GifDecoder();
    ~GifDecoder();
    GifDecoder(const GifDecoder&)            = delete;
    GifDecoder& operator=(const GifDecoder&) = delete;

    // Opens `data` as a GIF. Returns false if it is not a decodable GIF.
    [[nodiscard]] bool open(std::span<const uint8_t> data);

    // Next frame, or nullopt at end of stream / on a decode error.
    [[nodiscard]] std::optional<GifFrame> next_frame();

    // Seek back to the first frame, so the caller can loop.
    void rewind();

    [[nodiscard]] int    width()  const noexcept;
    [[nodiscard]] int    height() const noexcept;
    [[nodiscard]] size_t frames_decoded() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace media

#endif // OSV_VENDORED_AV
