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

} // namespace ui
