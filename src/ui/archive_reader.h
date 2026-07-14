#pragma once

#ifdef OSV_VENDORED_ARCHIVE

#include "crypto/secure_mem.h"
#include "ui/zip_plan.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
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
// True if `msg` (a libarchive archive_error_string()) indicates the extract
// failed specifically because of a missing/wrong password, as opposed to any
// other reason (corrupt entry, an encryption flavor this build's libarchive
// can't decrypt at all, I/O error, ...). Only the former should ever prompt
// the user to retry — see extract_failed_needs_password() below.
[[nodiscard]] inline bool archive_error_is_passphrase_issue(std::string_view msg) noexcept
{
    return msg.find("assphrase") != std::string_view::npos;
}


class ArchiveReader {
public:
    // Auto-detects the archive format (7z/RAR/TAR + gzip/xz filters) and
    // parses its entry list (name + is_dir) in one forward pass. Keeps a copy
    // of `data`. Returns false if libarchive can't recognise/open it at all.
    [[nodiscard]] bool open(std::span<const uint8_t> data, std::string_view passphrase = {});

    [[nodiscard]] const std::vector<ZipEntry>& entries() const noexcept { return entries_; }

    // Decompress the entry at `index` into an mlock'd buffer, replacing
    // `out`'s contents. Re-scans from the start of the archive. Returns false
    // if `index` is out of range, the entry is a directory, its declared size
    // is unknown or exceeds MAX_ENTRY_BYTES (corrupt/hostile size guard —
    // bounded before allocating, mirroring chunk_codec's orig_len check), or
    // decompression fails partway through.
    [[nodiscard]] bool extract(size_t index, crypto::SecureBytes& out) const;

    // Valid only immediately after extract() returns false (Phase 35): true
    // if that specific failure was a missing/wrong password (per
    // archive_error_is_passphrase_issue), false for every other failure
    // reason. Callers must check this before ever prompting for a password
    // retry — otherwise an encryption flavor this build can't decrypt at all
    // (e.g. WinZip AES, no crypto backend compiled in) would loop forever.
    [[nodiscard]] bool extract_failed_needs_password() const noexcept { return needs_password_; }

    // Sanity cap on a single entry's decompressed size: large enough for any
    // real photo/video, small enough that a corrupt or hostile archive
    // claiming an absurd size can't drive an oversized allocation attempt.
    static constexpr uint64_t MAX_ENTRY_BYTES = 4ULL * 1024 * 1024 * 1024;  // 4 GiB

private:
    std::vector<uint8_t>  data_;
    std::vector<ZipEntry> entries_;
    // Non-empty (with a trailing NUL byte, since SecureBytes zero-initialises
    // on resize) only when open() was given a passphrase. extract()'s
    // const char* view into this is only ever taken right before a libarchive
    // call — see open_stream() in the .cpp.

    crypto::SecureBytes   passphrase_;
    mutable bool          needs_password_ = false;

};

} // namespace ui

#endif // OSV_VENDORED_ARCHIVE
