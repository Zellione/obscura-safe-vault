#pragma once

#include <cstdint>
#include <vector>

#include "image/image.h"

// Transient pixel buffer passed to QRhi texture upload. Decoded pixels live
// here briefly on the heap (parity with image::ImageData in the existing app).
// RGBA format: 4 bytes per pixel, row-major, width*height*4 bytes total.
struct PixelBuffer {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;  // width * height * 4 bytes
};

// Helper: expand 3-channel RGB to 4-channel RGBA with opaque alpha (0xFF).
[[nodiscard]] inline PixelBuffer expand_rgb_to_rgba(const image::ImageData& img)
{
    PixelBuffer buf;
    buf.width = img.width;
    buf.height = img.height;

    const size_t pixel_count = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
    buf.rgba.reserve(pixel_count * 4);

    for (size_t i = 0; i < pixel_count; ++i) {
        // Each pixel is 3 bytes (RGB) in the source
        const size_t src_idx = i * 3;
        buf.rgba.push_back(img.pixels[src_idx]);      // R
        buf.rgba.push_back(img.pixels[src_idx + 1]);  // G
        buf.rgba.push_back(img.pixels[src_idx + 2]);  // B
        buf.rgba.push_back(0xFF);                     // A (opaque)
    }

    return buf;
}

// Helper: flatten RGBA to black background (alpha composition).
// For each pixel: output = input_rgb * (input_a / 255), with alpha = 0xFF.
// This ensures transparent areas composite as black instead of staying transparent.
[[nodiscard]] inline void flatten_alpha_to_black(PixelBuffer& buf)
{
    if (buf.rgba.empty()) {
        return;
    }

    const size_t pixel_count = static_cast<size_t>(buf.width) * static_cast<size_t>(buf.height);
    for (size_t i = 0; i < pixel_count; ++i) {
        const size_t idx = i * 4;
        const uint8_t r = buf.rgba[idx];
        const uint8_t g = buf.rgba[idx + 1];
        const uint8_t b = buf.rgba[idx + 2];
        const uint8_t a = buf.rgba[idx + 3];

        // Composite over black: out = in * (alpha / 255)
        buf.rgba[idx]     = static_cast<uint8_t>((static_cast<int>(r) * a) / 255);
        buf.rgba[idx + 1] = static_cast<uint8_t>((static_cast<int>(g) * a) / 255);
        buf.rgba[idx + 2] = static_cast<uint8_t>((static_cast<int>(b) * a) / 255);
        buf.rgba[idx + 3] = 0xFF;  // Set alpha to fully opaque
    }
}
