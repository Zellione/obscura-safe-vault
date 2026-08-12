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

// Whether an explicit Shift+C should open the compact-confirm modal.
//
// Phase 82: this is deliberately NOT should_display_waste. That predicate gates
// a passive footer HINT — a "significance" heuristic tuned so a busy grid is not
// nagged. Reusing it as the keypress's permission gate made compaction
// unreachable on exactly the vaults that needed it: on a 699 MiB vault the hint
// threshold is max(50 MiB, 10%) = 69.9 MiB, while auto_reclaim_space's own gate
// needs waste >= size/4 = 175 MiB, so a thousand small deletes freed nothing
// automatically AND were refused manually, with no figure shown either way.
// A keypress is a user request: any reclaimable waste earns the modal, which
// states the exact amount and defaults to cancel.
[[nodiscard]] inline bool should_offer_compact(uint64_t wasted_bytes) noexcept {
    return wasted_bytes > 0;
}

// Helper to check if waste from a cancelled import should be surfaced.
// "Significant" post-import-cancel if exceeds 1 MiB.
[[nodiscard]] inline bool should_hint_cancelled_import_waste(uint64_t wasted_bytes) {
    return wasted_bytes >= 1024 * 1024;  // >= 1 MiB
}

}  // namespace ui
