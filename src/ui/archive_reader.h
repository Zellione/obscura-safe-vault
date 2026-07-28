#pragma once

#ifdef OSV_VENDORED_ARCHIVE

#include "crypto/secure_mem.h"
#include "ui/zip_plan.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

struct archive;  // libarchive opaque handle (full type only in the .cpp)

namespace ui {

// Thin wrapper over libarchive's streaming read API (Phase 34): read-only
// access to 7z/RAR/TAR (+ gzip/xz-compressed TAR) archives entirely from an
// in-memory buffer — no temp file, matching the ZIP/CBZ path's invariant #1
// discipline. libarchive's read API is STREAMING ONLY (sequential
// archive_read_next_header/archive_read_data, no random access by index like
// miniz's central-directory reader), so open() does one forward pass to
// snapshot the entry list, and extract() holds a forward cursor: an open
// stream positioned just past the last extracted entry. Ascending extracts —
// the import loop's access pattern — continue that stream, so a full import
// is ONE pass over the archive; a target behind the cursor transparently
// reopens from the start. (Reopening per extract instead would re-decompress
// the whole prefix each time — O(n^2) total, ~150x slower than the zip path
// on a 150-entry solid 7z — while decompressing every entry up front could
// blow past mlock limits for a many-page archive.) ArchiveReader keeps its
// own copy of the source bytes so a re-scan is always possible after open()
// returns.
// True if `msg` (a libarchive archive_error_string()) indicates the extract
// failed specifically because of a missing/wrong password, as opposed to any
// other reason (corrupt entry, an encryption flavor this build's libarchive
// can't decrypt at all, I/O error, ...). Only the former should ever prompt
// the user to retry — see extract_failed_needs_password() below.
[[nodiscard]] inline bool archive_error_is_passphrase_issue(std::string_view msg) noexcept
{
    return msg.contains("assphrase");
}


class ArchiveReader {
public:
    ArchiveReader() = default;
    ~ArchiveReader();
    // The cached stream cursor owns a raw libarchive handle; nothing copies or
    // moves a reader (they are stack-local and passed by reference), so delete
    // both rather than write transfer semantics nobody uses.
    ArchiveReader(const ArchiveReader&)            = delete;
    ArchiveReader& operator=(const ArchiveReader&) = delete;
    ArchiveReader(ArchiveReader&&)                 = delete;
    ArchiveReader& operator=(ArchiveReader&&)      = delete;

    // Auto-detects the archive format (7z/RAR/TAR + gzip/xz filters) and
    // parses its entry list (name + is_dir) in one forward pass. Keeps a copy
    // of `data`. Returns false if libarchive can't recognise/open it at all.
    [[nodiscard]] bool open(std::span<const uint8_t> data, std::string_view passphrase = {});

    // Open a multi-volume archive using file paths (Phase 53: RAR multi-volume
    // support). `volumes` is a span of ordered volume paths (part 1, part 2, ...
    // in sequence). Uses libarchive's file-oriented API (archive_read_open_filenames)
    // which supports RAR4/5 multivolume. Returns false if the volume list is empty,
    // a path doesn't exist, isn't readable, isn't RAR, or the volumes are out of order.
    // Paths must be properly normalized before calling (see platform::normalize_user_path).
    [[nodiscard]] bool open_files(std::span<const std::filesystem::path> volumes,
                                   std::string_view passphrase = {});

    [[nodiscard]] const std::vector<ZipEntry>& entries() const noexcept { return entries_; }

    // Decompress the entry at `index` into an mlock'd buffer, replacing
    // `out`'s contents. Continues the forward cursor when `index` is at or
    // ahead of it; re-scans from the start otherwise. Returns false
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

    // Number of underlying libarchive stream opens performed by extract()
    // since the last open()/open_files(). Observable for the forward-cursor
    // perf contract (ascending extracts share one stream); asserted by tests,
    // no UI use.
    [[nodiscard]] size_t stream_opens() const noexcept { return stream_opens_; }

    // Sanity cap on a single entry's decompressed size: large enough for any
    // real photo/video, small enough that a corrupt or hostile archive
    // claiming an absurd size can't drive an oversized allocation attempt.
    static constexpr uint64_t MAX_ENTRY_BYTES = 4ULL * 1024 * 1024 * 1024;  // 4 GiB

    // Cap on the number of entries enumerated from one archive. libarchive
    // streams entries with no up-front count, so a hostile archive can declare
    // millions of (even empty) entries and drive `entries_` to exhaust memory.
    // Far above any real gallery archive (tens to a few hundred entries); a
    // count past it means the archive is hostile/corrupt, so open() fails closed.
    static constexpr size_t MAX_ENTRIES = 100000;

private:
    // Forward-scan the opened stream `a` into entries_, enforcing MAX_ENTRIES.
    // Returns false (having logged) on a fatal header error or once the cap is
    // exceeded; the caller frees `a` and clears its own state. Shared by
    // open() and open_files() so the cap can't drift between the two.
    [[nodiscard]] bool scan_entries(struct archive* a);

    // Free the cached stream cursor (if any) and rewind the logical position.
    // Called on any extract failure (per-call independence, exactly as if each
    // extract had its own stream), on re-open, and from the destructor.
    void reset_stream() const noexcept;

    std::vector<uint8_t>  data_;
    std::vector<ZipEntry> entries_;
    // Non-empty (with a trailing NUL byte, since SecureBytes zero-initialises
    // on resize) only when open() was given a passphrase. extract()'s
    // const char* view into this is only ever taken right before a libarchive
    // call — see open_stream() in the .cpp.

    crypto::SecureBytes   passphrase_;
    mutable bool          needs_password_ = false;
    mutable size_t        stream_opens_   = 0;

    // Forward stream cursor (see class comment): `stream_` is an open read
    // handle whose next archive_read_next_header() returns entry
    // `stream_next_`, or nullptr when no stream is cached. Mutable because
    // the cursor is a pure cache — extract() stays logically const.
    mutable struct archive* stream_      = nullptr;
    mutable size_t          stream_next_ = 0;

    // File paths for file-oriented archives (Phase 53: multi-volume RAR support).
    // If non-empty, extract() uses these paths; otherwise uses data_.
    std::vector<std::filesystem::path> file_paths_;

};

} // namespace ui

#endif // OSV_VENDORED_ARCHIVE
