#include "ui/meta_format.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <string_view>

#include "ui/playback_model.h"   // format_clock (shared time formatting)
#include "ui/video_exts.h"       // kVideoExts (shared video extension whitelist)

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
        case H264:      return "H.264";
        case HEVC:      return "H.265";
        case ProRes:    return "ProRes";
        case DNxHD:     return "DNxHD";
        case MJPEG:     return "MJPEG";
        case VP8:       return "VP8";
        case VP9:       return "VP9";
        case AV1:       return "AV1";
        case QTRLE:     return "QuickTime RLE";
        case Cinepak:   return "Cinepak";
        case MPEG1:     return "MPEG-1";
        case MPEG2:     return "MPEG-2";
        case MPEG4:     return "MPEG-4";
        case MSMPEG4V1: return "MS MPEG-4 v1";
        case MSMPEG4V2: return "MS MPEG-4 v2";
        case MSMPEG4V3: return "DivX 3";
        case WMV1:      return "WMV1";
        case WMV2:      return "WMV2";
        case WMV3:      return "WMV3";
        case VC1:       return "VC-1";
        case H263:      return "H.263";
        case FLV1:      return "Sorenson Spark";
        case VP6:       return "VP6";
        case VP6A:      return "VP6A";
        case VP6F:      return "VP6F";
        case SVQ1:      return "SVQ1";
        case SVQ3:      return "SVQ3";
        case DV:        return "DV";
        case MSVideo1:  return "MS Video 1";
        case RPZA:      return "RPZA";
        case HuffYUV:   return "HuffYUV";
        case FFV1:      return "FFV1";
        case Theora:    return "Theora";
        case RV10:      return "RealVideo 1.0";
        case RV20:      return "RealVideo 2.0";
        case RV30:      return "RealVideo 3.0";
        case RV40:      return "RealVideo 4.0";
        case Unknown:   break;
    }
    return "Video";
}

std::string_view video_container_name(vault::VideoContainer c) noexcept
{
    using enum vault::VideoContainer;
    switch (c) {
        case MP4:    return "MP4";
        case MKV:    return "MKV";
        case AVI:    return "AVI";
        case MPEGPS: return "MPEG-PS";
        case MPEGTS: return "MPEG-TS";
        case ASF:    return "ASF";
        case FLV:    return "FLV";
        case OGG:    return "Ogg";
        case RM:     return "RealMedia";
        case Unknown: break;
    }
    return "-";
}

std::string video_type_label(vault::VideoCodec c) noexcept
{
    if (c == vault::VideoCodec::Unknown) return "Video";
    return std::format("Video ({})", video_codec_name(c));
}

bool is_video_filename(std::string_view filename) noexcept
{
    const auto dot = filename.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= filename.size()) {
        return false;
    }
    std::string ext;
    for (const char c : filename.substr(dot + 1)) {
        ext.push_back(static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c));
    }
    return std::ranges::find(kVideoExts, ext) != kVideoExts.end();
}

} // namespace ui
