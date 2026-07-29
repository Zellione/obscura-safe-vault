// Stub implementations for image decode functions.
// Real codec implementations (decode_heif.cpp, decode_webp.cpp) are excluded from Qt builds
// to avoid static initialization conflicts with libheif/libaom's std::thread use.
// See CMakeLists.txt for details on the deadlock issue.

#include "image/decode.h"
#include "image/image.h"

#include <optional>
#include <span>

namespace image {

// These functions are stubbed in Qt builds. They return std::nullopt (decode failed).
// Real implementations use codec libraries that cause deadlock with Qt Scene Graph.
// For now, image decoding is unavailable in Qt builds (unblocks M2-M7 phases).

std::optional<ImageData> decode_webp_from_memory(const std::span<const uint8_t>)
{
    return std::nullopt;  // Stub: codec library excluded from Qt build
}

std::optional<ImageData> decode_heif_from_memory(const std::span<const uint8_t>)
{
    return std::nullopt;  // Stub: codec library excluded from Qt build
}

bool webp_is_animated(const std::span<const uint8_t>)
{
    return false;  // Stub
}

bool heif_is_animated(const std::span<const uint8_t>)
{
    return false;  // Stub
}

std::optional<ImageData> make_thumbnail(const ImageData&, int, int)
{
    return std::nullopt;  // Stub
}

std::optional<ImageData> decode_from_memory(const std::span<const uint8_t>)
{
    return std::nullopt;  // Stub: codec libraries excluded from Qt build
}

bool is_animated(ImageFormat, const std::span<const uint8_t>)
{
    return false;  // Stub
}

}  // namespace image
