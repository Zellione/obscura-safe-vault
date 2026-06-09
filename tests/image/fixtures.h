#pragma once

// In-memory image generation for tests.
// JPEG/PNG/BMP/TGA are encoded via stb_image_write (impl in thumbnail.cpp, linked
// into osv_tests — do NOT redefine STB_IMAGE_WRITE_IMPLEMENTATION here).
// GIF is hardcoded (stb_image_write has no GIF encoder).

#include <cstdint>
#include <vector>

namespace fixtures {

// Produce a solid-colour image encoded in the given format.
// (w, h) are the image dimensions; (r, g, b) fill every pixel.
std::vector<uint8_t> solid_jpeg(int w, int h, uint8_t r, uint8_t g, uint8_t b, int quality = 85);
std::vector<uint8_t> solid_png(int w, int h, uint8_t r, uint8_t g, uint8_t b);
std::vector<uint8_t> solid_bmp(int w, int h, uint8_t r, uint8_t g, uint8_t b);
std::vector<uint8_t> solid_tga(int w, int h, uint8_t r, uint8_t g, uint8_t b);

// Hardcoded minimal 1x1 red GIF89a (stb_image_write has no GIF encoder).
std::vector<uint8_t> gif_1x1_red();

// A buffer whose first two bytes look like a JPEG but is otherwise truncated,
// so that stb_image's decoder will reject it.
std::vector<uint8_t> malformed_jpeg();

} // namespace fixtures
