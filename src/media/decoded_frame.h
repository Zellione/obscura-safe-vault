#pragma once

// Decoded video frame representation: libav-free for cross-compilation.
// Planes point into the decoder's owned AVFrame (valid until next_frame/destruction).

#include <cstdint>

namespace media {

// Pixel format enum for decoded frames (Task 1 yields I420/NV12; Task 2 adds conversions).
enum class FramePixelFormat : uint8_t {
    I420,  // YUV 4:2:0 planar: Y plane, then U, then V (each U/V half the Y width/height)
    NV12,  // YUV 4:2:0 semi-planar: Y plane, then interleaved UV
};

// Decoded video frame: planes point into decoder's owned frame, valid until next call or destruction.
struct DecodedFrame {
    const uint8_t* planes[3];      // Y, U, V (or Y, UV for NV12)
    int            linesizes[3];   // bytes per row (may include padding)
    int            width;          // pixels
    int            height;         // pixels
    FramePixelFormat pix_fmt;      // I420 or NV12
    double         pts_seconds;    // presentation timestamp in seconds
};

} // namespace media
