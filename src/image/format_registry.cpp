#include "image/format_registry.h"

#include <cstring>

namespace image {

namespace {

// True if a 4-byte ASCII tag sits at `off` (bounds-checked).
bool tag_at(std::span<const uint8_t> d, size_t off, const char (&s)[5]) noexcept
{
    return d.size() >= off + 4 && std::memcmp(d.data() + off, s, 4) == 0;
}

} // namespace

ImageFormat detect_format(std::span<const uint8_t> d) noexcept
{
    using enum ImageFormat;
    if (d.empty()) return Unknown;

    // Single-signature raster formats (handled by stb_image).
    if (d.size() >= 2 && d[0] == 0xFF && d[1] == 0xD8)                               return JPEG;
    if (d.size() >= 4 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return PNG;
    if (d.size() >= 3 && d[0] == 'G'  && d[1] == 'I' && d[2] == 'F')                return GIF;
    if (d.size() >= 2 && d[0] == 'B'  && d[1] == 'M')                                return BMP;
    if (d.size() >= 2 && d[0] == '#'  && d[1] == '?')                                return HDR;

    // WebP: "RIFF" .... "WEBP".
    if (tag_at(d, 0, "RIFF") && tag_at(d, 8, "WEBP")) return WebP;

    // ISO base media file format: a "ftyp" box at offset 4, major brand at 8.
    // libheif decodes both HEIC (HEVC) and AVIF (AV1) from this container; we
    // distinguish them only to tag the image's format correctly.
    if (tag_at(d, 4, "ftyp")) {
        if (tag_at(d, 8, "avif") || tag_at(d, 8, "avis")) return AVIF;
        if (tag_at(d, 8, "heic") || tag_at(d, 8, "heix") ||
            tag_at(d, 8, "heim") || tag_at(d, 8, "heis") ||
            tag_at(d, 8, "mif1") || tag_at(d, 8, "msf1")) return HEIC;
    }

    // TGA has no reliable magic; it is left for the decoder as a last resort.
    return Unknown;
}

} // namespace image
