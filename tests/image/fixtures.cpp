// DO NOT define STB_IMAGE_WRITE_IMPLEMENTATION here: it is already defined in
// thumbnail.cpp, which is compiled into the same test binary. Including without
// the define gives extern declarations that the linker resolves at link time.
#include <stb_image_write.h>

#include "fixtures.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

#ifndef OSV_FIXTURE_DIR
#define OSV_FIXTURE_DIR "tests/image/fixtures"
#endif

namespace fixtures {

namespace {

using WriteVec = std::vector<uint8_t>;

void append_fn(void* ctx, void* data, int size)
{
    auto& out = *static_cast<WriteVec*>(ctx);
    const auto* p = static_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + size);
}

WriteVec solid_pixels(int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    WriteVec px(static_cast<size_t>(w) * h * 3);
    for (int i = 0; i < w * h; ++i) {
        px[i * 3 + 0] = r;
        px[i * 3 + 1] = g;
        px[i * 3 + 2] = b;
    }
    return px;
}

} // namespace

std::vector<uint8_t> solid_jpeg(int w, int h, uint8_t r, uint8_t g, uint8_t b, int quality)
{
    WriteVec out;
    const auto px = solid_pixels(w, h, r, g, b);
    stbi_write_jpg_to_func(append_fn, &out, w, h, 3, px.data(), quality);
    return out;
}

std::vector<uint8_t> solid_png(int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    WriteVec out;
    const auto px = solid_pixels(w, h, r, g, b);
    stbi_write_png_to_func(append_fn, &out, w, h, 3, px.data(), w * 3);
    return out;
}

std::vector<uint8_t> solid_bmp(int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    WriteVec out;
    const auto px = solid_pixels(w, h, r, g, b);
    stbi_write_bmp_to_func(append_fn, &out, w, h, 3, px.data());
    return out;
}

std::vector<uint8_t> solid_tga(int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    WriteVec out;
    const auto px = solid_pixels(w, h, r, g, b);
    stbi_write_tga_to_func(append_fn, &out, w, h, 3, px.data());
    return out;
}

std::vector<uint8_t> gif_1x1_red()
{
    // Minimal valid GIF89a: 1x1 red pixel.
    // GCT has 2 colors (packed 0x80 → size field 0 → 2^(0+1)=2 entries).
    // LZW min code size 2, stream: Clear(4), Index(0), EOI(5) → bytes 0x44 0x01.
    return {
        0x47, 0x49, 0x46, 0x38, 0x39, 0x61,  // "GIF89a"
        0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,  // LSD: 1x1, GCT, bgColor=0, PAR=0
        0xFF, 0x00, 0x00,  // GCT color 0: red
        0xFF, 0xFF, 0xFF,  // GCT color 1: white
        0x2C,              // image separator
        0x00, 0x00, 0x00, 0x00,  // left=0, top=0
        0x01, 0x00, 0x01, 0x00,  // width=1, height=1
        0x00,              // packed: no LCT, not interlaced
        0x02,              // LZW min code size
        0x02, 0x44, 0x01,  // data sub-block (size=2, encoded stream)
        0x00,              // block terminator
        0x3B,              // GIF trailer
    };
}

std::vector<uint8_t> malformed_jpeg()
{
    // JPEG magic bytes followed by a truncated/invalid header.
    return {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x01};
}

std::vector<uint8_t> load_fixture(const char* name)
{
    const std::string path = std::string(OSV_FIXTURE_DIR) + "/" + name;
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

} // namespace fixtures
