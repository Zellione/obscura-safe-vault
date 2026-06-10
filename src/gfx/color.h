#pragma once

#include <cstdint>

namespace gfx {

/// Straight (non-premultiplied) 8-bit RGBA colour. Defaults to opaque white so
/// `Color{}` is a sensible tint for textures and text.
struct Color {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
};

} // namespace gfx
