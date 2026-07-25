#pragma once

// Phase 53: PKWARE spanned/split ZIP merger.
//
// Merges multi-volume ZIP sets (.z01, .z02, …, .zip) into a single buffer
// suitable for miniz. Pure: no exceptions, no file I/O, no SDK dependencies.
//
// Input: array of volume buffers in order (z01 first, .zip last).
// Output: merged buffer with spanning marker stripped, all offsets made absolute,
//         disk fields zeroed to indicate single-disk archive.
//
// Rejects Zip64 archives (v1 limitation).

#include <cstdint>
#include <span>
#include <vector>

namespace ui {

enum class SpannedZipError : uint8_t {
    None = 0,
    NotSpanned,            // Not a spanned archive (doesn't start with marker)
    Zip64Unsupported,      // Zip64 EOCD/locator detected; not supported
    Malformed,             // Truncated, no EOCD, or invalid structure
};

struct SpannedZipResult {
    std::vector<uint8_t> merged;
    SpannedZipError error = SpannedZipError::None;
};

// Merge PKWARE multi-volume ZIP archives into a single buffer.
//
// volumes: array of volume buffers in order (.z01, .z02, …, .zip last).
//          Empty array or None return error.
//
// Returns: merged buffer with spanning marker stripped, all offsets rewritten
//          to absolute, and disk fields zeroed. On error, buffer is empty.
//
// Algorithm: strips leading 4-byte spanning marker from volume 1 (if present),
// concatenates all volumes, locates EOCD, recomputes CD offsets from disk-
// relative to absolute, rewrites EOCD disk fields to 0 (single-disk).
//
// [[nodiscard]] because ignoring errors silently corrupts imports.
[[nodiscard]] SpannedZipResult merge_spanned_zip(
    std::span<const std::span<const uint8_t>> volumes);

} // namespace ui
