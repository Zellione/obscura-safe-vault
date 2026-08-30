#pragma once

// The frame-decoder interface shared by every animated image format (Phase 57).
//
// Deliberately NOT VideoDecoder: no audio, no packet queues, no seeking, no
// hardware acceleration. One frame at a time, so memory stays constant no matter
// how long the animation is. Every emitted frame is a complete image — frame
// disposal and blending are the backend's job — in RGBA with alpha already
// flattened, which is what the existing image texture pipeline wants.
//
// This header pulls in neither FFmpeg nor libwebp, so it is safe to include
// anywhere, including in builds without vendored FFmpeg.
//
// Lifetime: open() BORROWS the caller's buffer (an mlock'd SecureBytes in
// production) and does not copy it. Keep it alive until the decoder is
// destroyed.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "crypto/secure_mem.h"

namespace media {

struct AnimFrame {
    crypto::SecureVector<uint8_t> rgba;  // width*height*4; alpha is always 255
    int    width   = 0;
    int    height  = 0;
    double delay_s = 0.0;           // >= kMinFrameDelay
};

// GIFs and WebPs commonly declare a 0 or 10 ms delay; every browser clamps to
// 20 ms, and so do we.
inline constexpr double kMinFrameDelay = 0.02;

class AnimDecoder {
public:
    virtual ~AnimDecoder() = default;

    AnimDecoder(const AnimDecoder&)            = delete;
    AnimDecoder& operator=(const AnimDecoder&) = delete;

    // Opens `data`. Returns false if it is not a decodable animation of this
    // decoder's format.
    [[nodiscard]] virtual bool open(std::span<const uint8_t> data) = 0;

    // Next frame, or nullopt at end of stream / on a decode error.
    [[nodiscard]] virtual std::optional<AnimFrame> next_frame() = 0;

    // Seek back to the first frame, so the caller can loop.
    virtual void rewind() = 0;

    [[nodiscard]] virtual int    width()  const noexcept         = 0;
    [[nodiscard]] virtual int    height() const noexcept         = 0;
    [[nodiscard]] virtual size_t frames_decoded() const noexcept = 0;

protected:
    AnimDecoder() = default;
};

} // namespace media
