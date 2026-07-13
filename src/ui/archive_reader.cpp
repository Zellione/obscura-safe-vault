#include "ui/archive_reader.h"

#ifdef OSV_VENDORED_ARCHIVE

#include <archive.h>
#include <archive_entry.h>

#include <print>

namespace ui {
namespace {

// Configure a fresh read handle over `data`: auto-detect the archive format
// (7z/RAR/TAR) and any compression filter (gzip/xz). Caller owns the result
// and must archive_read_free() it. Returns nullptr on open failure.
struct archive* open_stream(std::span<const uint8_t> data)
{
    struct archive* a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    if (archive_read_open_memory(a, data.data(), data.size()) != ARCHIVE_OK) {
        std::println(stderr, "[ArchiveReader] open failed: {}", archive_error_string(a));
        archive_read_free(a);
        return nullptr;
    }
    return a;
}

} // namespace

bool ArchiveReader::open(std::span<const uint8_t> data)
{
    data_.assign(data.begin(), data.end());
    entries_.clear();

    struct archive* a = open_stream(data_);
    if (!a) {
        data_.clear();
        return false;
    }

    struct archive_entry* entry = nullptr;
    for (;;) {
        const int r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF) break;
        if (r == ARCHIVE_FATAL || r < ARCHIVE_WARN) {
            // Hostile/corrupt archive mid-stream: fail closed rather than
            // return a partial entry list.
            std::println(stderr, "[ArchiveReader] header read failed: {}", archive_error_string(a));
            archive_read_free(a);
            data_.clear();
            entries_.clear();
            return false;
        }
        const char* path = archive_entry_pathname(entry);
        entries_.emplace_back(path ? std::string(path) : std::string{},
                              archive_entry_filetype(entry) == AE_IFDIR);
        archive_read_data_skip(a);
    }
    archive_read_free(a);
    return true;
}

bool ArchiveReader::extract(size_t index, crypto::SecureBytes& out) const
{
    if (index >= entries_.size() || entries_[index].is_dir) return false;

    struct archive* a = open_stream(data_);
    if (!a) return false;

    // A while loop (not a for loop) so the loop header stays concerned only
    // with `i`/`found` control, and a single break covers the one early-exit
    // case (a fatal/EOF read mid-stream) — `found` naturally ends the loop
    // via the condition once the target header has been read.
    struct archive_entry* entry = nullptr;
    bool found = false;
    size_t i = 0;
    while (!found && i <= index) {
        if (const int r = archive_read_next_header(a, &entry);
            r == ARCHIVE_EOF || (r != ARCHIVE_OK && r < ARCHIVE_WARN))
            break;
        found = (i == index);
        if (!found) archive_read_data_skip(a);
        ++i;
    }
    if (!found) {
        archive_read_free(a);
        return false;
    }

    if (!archive_entry_size_is_set(entry)) {
        archive_read_free(a);
        return false;
    }
    const int64_t declared = archive_entry_size(entry);
    if (declared < 0 || static_cast<uint64_t>(declared) > MAX_ENTRY_BYTES) {
        archive_read_free(a);
        return false;
    }

    if (!out.resize(static_cast<size_t>(declared))) {
        archive_read_free(a);
        return false;
    }

    size_t total = 0;
    while (total < out.size()) {
        const la_ssize_t n = archive_read_data(a, out.data() + total, out.size() - total);
        if (n < 0) {
            std::println(stderr, "[ArchiveReader] data read failed: {}", archive_error_string(a));
            out.wipe();
            (void)out.resize(0);
            archive_read_free(a);
            return false;
        }
        if (n == 0) break;  // short read: entry had less data than declared
        total += static_cast<size_t>(n);
    }
    archive_read_free(a);
    if (total != out.size()) {
        out.wipe();
        (void)out.resize(0);
        return false;
    }
    return true;
}

} // namespace ui

#endif // OSV_VENDORED_ARCHIVE
