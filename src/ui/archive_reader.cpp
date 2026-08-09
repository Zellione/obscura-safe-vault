#include "ui/archive_reader.h"

#ifdef OSV_VENDORED_ARCHIVE

#include <archive.h>
#include <archive_entry.h>

#include <cstring>
#include <print>

#include "platform/path_utf8.h"

namespace ui {
namespace {

// The const char* open_stream() needs, or nullptr when no passphrase was
// given — a single place to avoid every call site re-deriving this.
const char* passphrase_cstr(const crypto::SecureBytes& passphrase)
{
    return passphrase.empty() ? nullptr : reinterpret_cast<const char*>(passphrase.data());
}

// Configure a fresh read handle over `data`: auto-detect the archive format
// (7z/RAR/TAR) and any compression filter (gzip/xz). Caller owns the result
// and must archive_read_free() it. Returns nullptr on open failure.
struct archive* open_stream(std::span<const uint8_t> data, const char* passphrase)
{
    struct archive* a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    if (passphrase) archive_read_add_passphrase(a, passphrase);
    if (archive_read_open_memory(a, data.data(), data.size()) != ARCHIVE_OK) {
        std::println(stderr, "[ArchiveReader] open failed: {}", archive_error_string(a));
        archive_read_free(a);
        return nullptr;
    }
    return a;
}

// Configure a fresh read handle over file paths: auto-detect the archive format
// and any compression filter. Used for multi-volume archives. Caller owns the
// result and must archive_read_free() it. Returns nullptr on open failure.
// `file_paths` is a vector of filesystem::path; ownership remains with caller.
struct archive* open_stream_files(const std::vector<std::filesystem::path>& file_paths,
                                   const char* passphrase)
{
    // Convert to C strings in a vector that stays alive for archive_read_open_filenames
    std::vector<std::string> vol_strings;
    for (const auto& vol : file_paths) {
        vol_strings.push_back(platform::path_to_utf8(vol));
    }

    std::vector<const char*> vol_cstrs;
    for (const auto& vol_str : vol_strings) {
        vol_cstrs.push_back(vol_str.c_str());
    }
    vol_cstrs.push_back(nullptr);  // NULL-terminate

    struct archive* a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    if (passphrase) archive_read_add_passphrase(a, passphrase);
    if (archive_read_open_filenames(a, vol_cstrs.data(), 10240) != ARCHIVE_OK) {
        std::println(stderr, "[ArchiveReader] open_files failed: {}", archive_error_string(a));
        archive_read_free(a);
        return nullptr;
    }
    return a;
}

} // namespace

bool ArchiveReader::scan_entries(struct archive* a)
{
    struct archive_entry* entry = nullptr;
    for (;;) {
        const int r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF) {
            return true;
        }
        if (r == ARCHIVE_FATAL || r < ARCHIVE_WARN) {
            // Hostile/corrupt archive mid-stream: fail closed rather than
            // return a partial entry list.
            std::println(stderr, "[ArchiveReader] header read failed: {}", archive_error_string(a));
            return false;
        }
        if (entries_.size() >= MAX_ENTRIES) {
            // Too many entries to be a real gallery archive; refuse rather than
            // let an unbounded list exhaust memory.
            std::println(stderr, "[ArchiveReader] archive declares more than {} entries — refusing",
                         MAX_ENTRIES);
            return false;
        }
        const char* path = archive_entry_pathname(entry);
        entries_.emplace_back(path ? std::string(path) : std::string{},
                              archive_entry_filetype(entry) == AE_IFDIR);
        archive_read_data_skip(a);
    }
}

ArchiveReader::~ArchiveReader()
{
    reset_stream();
}

void ArchiveReader::reset_stream() const noexcept
{
    if (stream_) {
        archive_read_free(stream_);
        stream_ = nullptr;
    }
    stream_next_ = 0;
}

bool ArchiveReader::open(std::span<const uint8_t> data, std::string_view passphrase)
{
    reset_stream();
    data_.assign(data.begin(), data.end());
    entries_.clear();
    stream_opens_ = 0;
    passphrase_.wipe();
    (void)passphrase_.resize(0);
    if (!passphrase.empty()) {
        if (!passphrase_.resize(passphrase.size() + 1)) {  // +1: trailing NUL for the C API
            data_.clear();
            return false;
        }
        std::memcpy(passphrase_.data(), passphrase.data(), passphrase.size());
    }

    struct archive* a = open_stream(data_, passphrase_cstr(passphrase_));
    if (!a) {
        data_.clear();
        passphrase_.wipe();
        (void)passphrase_.resize(0);
        return false;
    }

    if (!scan_entries(a)) {
        archive_read_free(a);
        data_.clear();
        entries_.clear();
        return false;
    }
    archive_read_free(a);
    return true;
}

bool ArchiveReader::extract(size_t index, crypto::SecureBytes& out) const
{
    needs_password_ = false;
    if (index >= entries_.size() || entries_[index].is_dir) return false;

    // Forward cursor: libarchive streams can't seek, so reaching entry N
    // means decompressing everything before it. Reuse the open stream whenever
    // the target is at or ahead of the cursor — the import loop's ascending
    // access pattern then costs ONE pass over the archive instead of a reopen
    // per entry (which re-decompresses the whole prefix each time: O(n^2)
    // total, measured ~150x slower than the zip path on a 150-entry solid 7z).
    // A target behind the cursor transparently falls back to a fresh stream.
    if (!stream_ || index < stream_next_) {
        reset_stream();
        // Use file-based extraction if file_paths_ is populated (multi-volume),
        // otherwise use memory-based extraction
        stream_ = file_paths_.empty()
            ? open_stream(data_, passphrase_cstr(passphrase_))
            : open_stream_files(file_paths_, passphrase_cstr(passphrase_));
        if (!stream_) return false;
        ++stream_opens_;
    }
    struct archive* const a = stream_;

    // A while loop (not a for loop) so the loop header stays concerned only
    // with cursor/`found` control, and a single break covers the one early-exit
    // case (a fatal/EOF read mid-stream) — `found` naturally ends the loop
    // via the condition once the target header has been read. Note
    // archive_read_next_header implicitly discards any unread tail of the
    // previous entry, so the cursor cannot desync after a successful extract.
    struct archive_entry* entry = nullptr;
    bool found = false;
    while (!found && stream_next_ <= index) {
        if (const int r = archive_read_next_header(a, &entry);
            r == ARCHIVE_EOF || (r != ARCHIVE_OK && r < ARCHIVE_WARN))
            break;
        found = (stream_next_ == index);
        if (!found) archive_read_data_skip(a);
        ++stream_next_;
    }
    if (!found) {
        reset_stream();
        return false;
    }

    if (!archive_entry_size_is_set(entry)) {
        reset_stream();
        return false;
    }
    const int64_t declared = archive_entry_size(entry);
    if (declared < 0 || static_cast<uint64_t>(declared) > MAX_ENTRY_BYTES) {
        reset_stream();
        return false;
    }

    if (!out.resize(static_cast<size_t>(declared))) {
        reset_stream();
        return false;
    }

    size_t total = 0;
    while (total < out.size()) {
        const la_ssize_t n = archive_read_data(a, out.data() + total, out.size() - total);
        if (n < 0) {
            const std::string_view msg = archive_error_string(a);
            std::println(stderr, "[ArchiveReader] data read failed: {}", msg);
            needs_password_ = archive_error_is_passphrase_issue(msg);
            out.wipe();
            (void)out.resize(0);
            reset_stream();
            return false;
        }
        if (n == 0) break;  // short read: entry had less data than declared
        total += static_cast<size_t>(n);
    }
    if (total != out.size()) {
        out.wipe();
        (void)out.resize(0);
        reset_stream();
        return false;
    }

    // Force libarchive to finalize the entry and validate its CRC. ZipCrypto
    // verifies a wrong password against a single check byte, so ~1 wrong
    // password in 256 false-accepts and decrypts to garbage of the right
    // length; the CRC mismatch surfaces only here, on the read past the last
    // declared byte. Without this probe extract() would return that garbage as
    // success and the importer would write it — breaking the "nothing written
    // on a wrong password" invariant (CWE-noise, but a real write of
    // attacker-controlled bytes). A clean entry returns 0 (entry EOF) here.
    uint8_t sentinel = 0;
    if (const la_ssize_t n = archive_read_data(a, &sentinel, 1); n < 0) {
        const std::string_view msg = archive_error_string(a);
        std::println(stderr, "[ArchiveReader] entry finalize failed: {}", msg);
        needs_password_ = archive_error_is_passphrase_issue(msg);
        out.wipe();
        (void)out.resize(0);
        reset_stream();
        return false;
    }
    // Success: keep the stream open for the next (ascending) extract.
    return true;
}

bool ArchiveReader::open_files(std::span<const std::filesystem::path> volumes,
                                std::string_view passphrase)
{
    reset_stream();
    entries_.clear();
    file_paths_.clear();
    stream_opens_ = 0;
    passphrase_.wipe();
    (void)passphrase_.resize(0);

    // Empty volume list is invalid
    if (volumes.empty()) return false;

    // Store passphrase if provided (same as open())
    if (!passphrase.empty()) {
        if (!passphrase_.resize(passphrase.size() + 1)) {  // +1: trailing NUL for the C API
            return false;
        }
        std::memcpy(passphrase_.data(), passphrase.data(), passphrase.size());
    }

    // Store file paths for extraction later
    file_paths_.assign(volumes.begin(), volumes.end());

    // Open via the file-oriented API
    struct archive* a = open_stream_files(file_paths_, passphrase_cstr(passphrase_));
    if (!a) {
        file_paths_.clear();
        passphrase_.wipe();
        (void)passphrase_.resize(0);
        return false;
    }

    // Parse the entry list in one forward pass (shared with open()).
    if (!scan_entries(a)) {
        archive_read_free(a);
        entries_.clear();
        file_paths_.clear();
        passphrase_.wipe();
        (void)passphrase_.resize(0);
        return false;
    }
    archive_read_free(a);
    return true;
}

} // namespace ui

#endif // OSV_VENDORED_ARCHIVE
