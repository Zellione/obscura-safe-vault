#pragma once

#include <algorithm>
#include <cstdint>

namespace ui {

// Pure helper to determine if waste should be displayed as a hint.
// Waste is "significant" (worth showing) if it exceeds max(50 MiB, 10% of file size).
[[nodiscard]] inline bool should_display_waste(uint64_t wasted_bytes, uint64_t vault_file_size) {
    if (wasted_bytes == 0) return false;
    const uint64_t min_absolute = 50 * 1024 * 1024;
    const uint64_t threshold = (vault_file_size > 0)
        ? std::max(min_absolute, vault_file_size / 10)
        : min_absolute;
    return wasted_bytes >= threshold;
}

// Helper to check if waste from a cancelled import should be surfaced.
// "Significant" post-import-cancel if exceeds 1 MiB.
[[nodiscard]] inline bool should_hint_cancelled_import_waste(uint64_t wasted_bytes) {
    return wasted_bytes >= 1024 * 1024;  // >= 1 MiB
}

}  // namespace ui
