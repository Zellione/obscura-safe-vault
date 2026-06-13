#include "ui/meta_format.h"

#include <array>
#include <chrono>
#include <format>

namespace ui {

std::string format_size(uint64_t bytes)
{
    if (bytes < 1024) return std::format("{} B", bytes);

    constexpr std::array<const char*, 3> units{"KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    size_t u = 0;
    value /= 1024.0;                                  // -> KB
    while (value >= 1024.0 && u + 1 < units.size()) {
        value /= 1024.0;
        ++u;
    }
    return std::format("{:.1f} {}", value, units[u]);
}

std::string format_dimensions(uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0) return "-";
    return std::format("{}x{}", w, h);
}

std::string format_date(uint64_t unix_seconds)
{
    if (unix_seconds == 0) return "-";
    const std::chrono::sys_seconds tp{std::chrono::seconds{static_cast<int64_t>(unix_seconds)}};
    return std::format("{:%Y-%m-%d}", std::chrono::floor<std::chrono::days>(tp));
}

std::string_view image_format_name(vault::ImageFormat f) noexcept
{
    using enum vault::ImageFormat;
    switch (f) {
        case JPEG: return "JPEG";
        case PNG:  return "PNG";
        case GIF:  return "GIF";
        case BMP:  return "BMP";
        case TGA:  return "TGA";
        case HDR:  return "HDR";
        case WebP: return "WebP";
        case HEIC: return "HEIC";
        case AVIF: return "AVIF";
        case Unknown: break;
    }
    return "-";
}

} // namespace ui
