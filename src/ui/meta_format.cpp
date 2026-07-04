#include "ui/meta_format.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <string_view>

#include "ui/playback_model.h"   // format_clock (shared time formatting)

namespace ui {

std::string format_size(uint64_t bytes)
{
    if (bytes < 1024) return std::format("{} B", bytes);

    constexpr std::array<const char*, 3> units{"KB", "MB", "GB"};
    auto value = static_cast<double>(bytes);
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

std::string format_duration(uint64_t microseconds)
{
    return format_clock(static_cast<double>(microseconds) / 1'000'000.0);
}

std::string_view video_codec_name(vault::VideoCodec c) noexcept
{
    using enum vault::VideoCodec;
    switch (c) {
        case H264:   return "H.264";
        case HEVC:   return "H.265";
        case ProRes: return "ProRes";
        case DNxHD:  return "DNxHD";
        case MJPEG:  return "MJPEG";
        case Unknown: break;
    }
    return "Video";
}

std::string video_type_label(vault::VideoCodec c) noexcept
{
    if (c == vault::VideoCodec::Unknown) return "Video";
    return std::format("Video ({})", video_codec_name(c));
}

bool is_video_filename(std::string_view filename) noexcept
{
    const auto dot = filename.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= filename.size()) return false;
    std::string ext;
    for (const char c : filename.substr(dot + 1))
        ext.push_back(static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c));
    static constexpr std::array<std::string_view, 5> kVideoExts{"mp4", "mkv", "webm", "mov", "m4v"};
    return std::ranges::find(kVideoExts, ext) != kVideoExts.end();
}

} // namespace ui
