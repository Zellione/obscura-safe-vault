#include "ui/meta_format.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <string_view>
#include <utility>

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
    struct CodecName {
        vault::VideoCodec codec;
        std::string_view  name;
    };
    // One row per codec. A table rather than a switch: the codec list is a long
    // flat mapping that keeps growing, and the static_assert below preserves the
    // exhaustiveness a switch-without-default gave — adding an enumerator without
    // a display name here is a build error.
    static constexpr std::array<CodecName, 37> kNames{{
        {H264,      "H.264"},
        {HEVC,      "H.265"},
        {ProRes,    "ProRes"},
        {DNxHD,     "DNxHD"},
        {MJPEG,     "MJPEG"},
        {VP8,       "VP8"},
        {VP9,       "VP9"},
        {AV1,       "AV1"},
        {QTRLE,     "QuickTime RLE"},
        {Cinepak,   "Cinepak"},
        {MPEG1,     "MPEG-1"},
        {MPEG2,     "MPEG-2"},
        {MPEG4,     "MPEG-4"},
        {MSMPEG4V1, "MS MPEG-4 v1"},
        {MSMPEG4V2, "MS MPEG-4 v2"},
        {MSMPEG4V3, "DivX 3"},
        {WMV1,      "WMV1"},
        {WMV2,      "WMV2"},
        {WMV3,      "WMV3"},
        {VC1,       "VC-1"},
        {H263,      "H.263"},
        {FLV1,      "Sorenson Spark"},
        {VP6,       "VP6"},
        {VP6A,      "VP6A"},
        {VP6F,      "VP6F"},
        {SVQ1,      "SVQ1"},
        {SVQ3,      "SVQ3"},
        {DV,        "DV"},
        {MSVideo1,  "MS Video 1"},
        {RPZA,      "RPZA"},
        {HuffYUV,   "HuffYUV"},
        {FFV1,      "FFV1"},
        {Theora,    "Theora"},
        {RV10,      "RealVideo 1.0"},
        {RV20,      "RealVideo 2.0"},
        {RV30,      "RealVideo 3.0"},
        {RV40,      "RealVideo 4.0"},
    }};
    // VideoCodec's enumerators are dense 0..RV40 (Unknown is 0xFF and falls
    // through to "Video" below), so one row per value means size == RV40 + 1.
    static_assert(kNames.size() == std::to_underlying(RV40) + 1U,
                  "Add a display name whenever a VideoCodec enumerator is added");

    const auto* it = std::ranges::find_if(kNames, [c](const CodecName& e) { return e.codec == c; });
    return it != kNames.end() ? it->name : "Video";
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
