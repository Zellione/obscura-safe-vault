#pragma once

// Phase 51: walk a user-picked directory into the ZipEntry shape an archive
// reader produces, so build_zip_plan() mirrors it into sub-galleries with no
// second tree-building implementation.
//
// Deliberately does NOT filter by extension — build_zip_plan owns the
// "supported media" decision and counts what it skips, so filtering here would
// silently deflate that tally.

#include <cstddef>
#include <filesystem>
#include <vector>

#include "ui/zip_plan.h"   // ZipEntry

namespace ui {

struct ScanLimits {
    // Bound a mistakenly-picked huge root (e.g. "/"). Hit => the walk stops.
    size_t max_entries = 100000;
};

// Recursively list `root`'s contents as '/'-separated paths RELATIVE to `root`.
// Symlinks are skipped outright — following them risks directory cycles and
// lets the walk escape the picked root. Permission-denied subtrees are skipped
// rather than aborting the walk. A missing/unreadable root yields an empty list.
[[nodiscard]] std::vector<ZipEntry> scan_folder(const std::filesystem::path& root,
                                                ScanLimits limits = {});

} // namespace ui
