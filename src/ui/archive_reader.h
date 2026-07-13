#pragma once

#ifdef OSV_VENDORED_ARCHIVE

#include "crypto/secure_mem.h"
#include "ui/zip_plan.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ui {

// Thin wrapper over libarchive's streaming read API (Phase 34): read-only
// access to 7z/RAR/TAR (+ gzip/xz-compressed TAR) archives entirely from an
// in-memory buffer — no temp file, matching the ZIP/CBZ path's invariant #1
// discipline. libarchive's read API is STREAMING ONLY (sequential
// archive_read_next_header/archive_read_data, no random access by index like
// miniz's central-directory reader), so open() does one forward pass to
// snapshot the entry list, and extract() re-opens a fresh stream and re-scans
// forward to the requested index each time, rather than decompressing every
// entry up front and holding them all in memory simultaneously (which could
// blow past mlock limits for a many-page archive). O(n) per extract, O(n^2)
// for a full sequential import — fine for typical gallery sizes (tens to a
// few hundred entries). ArchiveReader keeps its own copy of the source bytes
// so a re-scan is always possible after open() returns.
class ArchiveReader {
public:
    // Auto-detects the archive format (7z/RAR/TAR + gzip/xz filters) and
    // parses its entry list (name + is_dir) in one forward pass. Keeps a copy
    // of `data`. Returns false if libarchive can't recognise/open it at all.
    [[nodiscard]] bool open(std::span<const uint8_t> data);

    [[nodiscard]] const std::vector<ZipEntry>& entries() const noexcept { return entries_; }

    // Decompress the entry at `index` into an mlock'd buffer, replacing
    // `out`'s contents. Re-scans from the start of the archive. Returns false
    // if `index` is out of range, the entry is a directory, its declared size
    // is unknown or exceeds MAX_ENTRY_BYTES (corrupt/hostile size guard —
    // bounded before allocating, mirroring chunk_codec's orig_len check), or
    // decompression fails partway through.
    [[nodiscard]] bool extract(size_t index, crypto::SecureBytes& out) const;

    // Sanity cap on a single entry's decompressed size: large enough for any
    // real photo/video, small enough that a corrupt or hostile archive
    // claiming an absurd size can't drive an oversized allocation attempt.
    static constexpr uint64_t MAX_ENTRY_BYTES = 4ULL * 1024 * 1024 * 1024;  // 4 GiB

private:
    std::vector<uint8_t>  data_;
    std::vector<ZipEntry> entries_;
};

} // namespace ui

#endif // OSV_VENDORED_ARCHIVE
