#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "vault/index.h"

// Pure formatting helpers for the gallery's detailed list view. SDL- and IO-free
// so they can be unit-tested headlessly.
namespace ui {

// Human-readable byte count: "512 B", "1.5 KB", "2.3 MB", "1.0 GB" (1024-based).
[[nodiscard]] std::string format_size(uint64_t bytes);

// "1920x1080", or "-" when either dimension is unknown (0).
[[nodiscard]] std::string format_dimensions(uint32_t w, uint32_t h);

// UTC calendar date "YYYY-MM-DD" for a Unix-seconds timestamp ("-" when 0).
[[nodiscard]] std::string format_date(uint64_t unix_seconds);

// Short uppercase format name ("JPEG", "PNG", ...); "-" for Unknown.
[[nodiscard]] std::string_view image_format_name(vault::ImageFormat f) noexcept;

// Video duration from microseconds, as "m:ss" / "h:mm:ss" (via format_clock).
[[nodiscard]] std::string format_duration(uint64_t microseconds);

// Codec display name ("H.264", "H.265"); "Video" for Unknown.
[[nodiscard]] std::string_view video_codec_name(vault::VideoCodec c) noexcept;

// Container display name ("MP4", "MKV"); "-" for Unknown.
[[nodiscard]] std::string_view video_container_name(vault::VideoContainer c) noexcept;

// List-view type label: "Video (H.264)" etc.; bare "Video" for Unknown.
[[nodiscard]] std::string video_type_label(vault::VideoCodec c) noexcept;

// True if `filename`'s extension names a supported video container (case-
// insensitive): mp4, mkv, webm, mov, m4v. Used to route imports to add_video.
[[nodiscard]] bool is_video_filename(std::string_view filename) noexcept;

} // namespace ui
