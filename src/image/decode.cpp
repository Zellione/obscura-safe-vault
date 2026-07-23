#include <cstdlib>
#include <cstring>

// Zero-initialise every stb_image allocation. On malformed JPEGs stb's colour
// conversion can read plane regions the aborted decode never wrote; with plain
// malloc those pixels would expose stale heap bytes (e.g. a previously decoded
// image) in the output. Grow via a fresh calloc + copy so the tail stays zero.
// Must keep realloc semantics: on failure the old block stays alive (stb's
// error paths still free it). void* and the pre-#include placement are both
// mandated by stb's C allocator macro contract (call sites pass char*/int*).
static void* stbi_realloc_zeroed(void* p, size_t oldsz, size_t newsz) // NOSONAR cpp:S5008 cpp:S954
{
    void* q = std::calloc(1, newsz); // NOSONAR cpp:S1231 — stb frees via STBI_FREE, so the shim must stay malloc-family
    if (!q) return nullptr;
    if (p) {
        std::memcpy(q, p, oldsz < newsz ? oldsz : newsz);
        std::free(p); // NOSONAR cpp:S1231 — releasing a block stb allocated through this same C contract
    }
    return q;
}
#define STBI_MALLOC(sz) std::calloc(1, (sz))
#define STBI_REALLOC_SIZED(p, oldsz, newsz) stbi_realloc_zeroed((p), (oldsz), (newsz))
#define STBI_FREE(p) std::free(p)

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "image/decode.h"
#include "image/decoder.h"
#include "image/format_registry.h"

namespace image {

namespace {

// stb_image path for JPEG/PNG/GIF/BMP/TGA/HDR. `fmt` is the registry's verdict;
// TGA has no magic (registry returns Unknown), so Unknown is tagged as TGA — stb
// is the last-resort decoder for headerless raster data.
std::optional<ImageData> decode_stb(std::span<const uint8_t> data, ImageFormat fmt)
{
    int w      = 0;
    int h      = 0;
    int src_ch = 0;
    // Force 3-channel RGB: simplifies thumbnail generation and GPU upload.
    stbi_uc* raw = stbi_load_from_memory(
        data.data(), static_cast<int>(data.size()), &w, &h, &src_ch, 3);
    if (!raw) return std::nullopt;

    ImageData img;
    img.width  = w;
    img.height = h;
    img.format = (fmt == ImageFormat::Unknown) ? ImageFormat::TGA : fmt;
    img.pixels.assign(raw, raw + static_cast<size_t>(w) * h * 3);
    stbi_image_free(raw);
    return img;
}

// Last-resort decoder: stb handles JPEG/PNG/GIF/BMP/HDR (by magic) and TGA
// (headerless). It claims every buffer, so it must be registered last.
class StbDecoder final : public Decoder {
public:
    [[nodiscard]] bool can_decode(std::span<const uint8_t>) const noexcept override { return true; }
    [[nodiscard]] std::optional<ImageData> decode(std::span<const uint8_t> data) const override
    {
        return decode_stb(data, detect_format(data));
    }
};

} // namespace

std::unique_ptr<Decoder> make_stb_decoder() { return std::make_unique<StbDecoder>(); }

std::optional<ImageData> decode_from_memory(std::span<const uint8_t> data)
{
    if (data.empty()) return std::nullopt;
    return default_registry().decode(data);
}

} // namespace image
