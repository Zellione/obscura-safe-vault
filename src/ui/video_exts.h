#pragma once

#include <array>
#include <string_view>

namespace ui {

// Unified video extension whitelist. Originally defined identically in
// zip_plan.cpp and meta_format.cpp; extracted here to prevent drift.
// Includes original supported formats (mp4, mkv, webm, mov, m4v) plus
// legacy container formats (avi, mpg, mpeg, wmv, asf, flv, ts, m2ts, ogv, rm, rmvb).
// Used for filename validation (is_video_filename, ext_in) and file dialog filters.
inline constexpr std::array<std::string_view, 16> kVideoExts{
    "mp4", "mkv", "webm", "mov", "m4v",           // original 5
    "avi", "mpg", "mpeg", "wmv", "asf",           // legacy container formats
    "flv", "ts", "m2ts", "ogv", "rm", "rmvb"      // more legacy formats
};

}  // namespace ui
