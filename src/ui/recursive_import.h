#pragma once

// Phase 53: recursive extraction of archives nested inside archives.
//
// This header currently carries the naming rules; the depth-first orchestrator
// and its guards land in the same module.
//
// Pure: no miniz, no libarchive, no vault, no SDL.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

// Sub-gallery name for a nested archive: extension stripped, then run through
// vault::sanitize_node_name (invariant 6 — an archive entry name is untrusted
// and becomes a path component). "bonus.tar.gz" -> "bonus", not "bonus.tar".
//
// Returns "" when nothing usable survives; the caller substitutes a fallback
// rather than creating an unnamed gallery.
[[nodiscard]] std::string nested_gallery_name(std::string_view archive_filename);

// `base` if unused, else `base_2`, `base_3`, … skipping anything already in
// `taken`. Comparison is exact — it mirrors Vault::create_gallery's duplicate
// rule, which is what would otherwise reject the second sub-gallery.
[[nodiscard]] std::string unique_gallery_name(std::string_view                base,
                                              const std::vector<std::string>& taken);

// --- recursion budget -------------------------------------------------------
//
// A nested archive is untrusted input, and recursion turns a small file into an
// unbounded amount of work. Five independent limits bound it; each one fails
// only the offending BRANCH, never the whole import, so one hostile archive
// buried in a legitimate set cannot destroy the rest of the job.

struct RecursionLimits {
    int      max_depth            = 16;
    uint64_t max_total_expanded   = 8ULL * 1024 * 1024 * 1024;  // whole recursion
    uint64_t max_live_bytes       = 1ULL * 1024 * 1024 * 1024;  // current root->leaf path
    int      max_nested_archives  = 4096;
    // Expanded bytes per byte of the ORIGINAL top-level archive. A zip bomb's
    // whole trick is a tiny file that expands without bound.
    uint64_t max_expansion_ratio  = 1000;
};

enum class RecursionVerdict : uint8_t {
    Allow,
    DepthExceeded,
    TotalBytesExceeded,
    LiveBytesExceeded,
    CountExceeded,
    RatioExceeded,
};

// Tracks the running totals behind those limits. Pure: no I/O, no allocation
// beyond its own counters, so every limit is unit-testable without an archive.
class RecursionBudget {
public:
    RecursionBudget(RecursionLimits limits, uint64_t root_archive_bytes);

    // May a nested archive of `nested_bytes` be entered at `depth`? Does not
    // mutate; call enter() only after an Allow.
    [[nodiscard]] RecursionVerdict may_descend(int depth, uint64_t nested_bytes) const;

    // Record entering / leaving a nested archive, maintaining the live-bytes
    // total along the current path.
    void enter(uint64_t nested_bytes);
    void leave(uint64_t nested_bytes);

    // Record bytes produced by extracting entries (media or nested archives).
    void note_expanded(uint64_t bytes);

    [[nodiscard]] uint64_t live_bytes() const noexcept { return live_bytes_; }
    [[nodiscard]] uint64_t total_expanded() const noexcept { return total_expanded_; }
    [[nodiscard]] int      nested_seen() const noexcept { return nested_seen_; }

private:
    RecursionLimits limits_;
    uint64_t        root_bytes_     = 0;
    uint64_t        live_bytes_     = 0;
    uint64_t        total_expanded_ = 0;
    int             nested_seen_    = 0;
};

} // namespace ui
