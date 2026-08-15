#pragma once

// Phase 84: one open ArchiveReader per live archive buffer during a
// recursive import. Before this cache, the stateless recursive-import hooks
// built a fresh ArchiveReader per extracted entry; on a solid archive each
// open() is a full forward re-scan, so an N-entry import decompressed
// O(N^2) bytes total (the PR #124 class, reintroduced above the reader).
//
// Keyed on (data pointer, size). That identifies an archive ONLY while its
// buffer is alive — the walker must drop() an archive when its frame pops,
// BEFORE the buffer dies, or a later allocation reusing the address would
// alias a stale reader. walk_archive's archive_done hook (Phase 84) is that
// drop point; eviction also bounds memory, since each ArchiveReader keeps
// its own copy of the archive bytes.

#ifdef OSV_VENDORED_ARCHIVE

#include "ui/archive_reader.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>

namespace ui {

class ArchiveReaderCache {
public:
    // The reader for `bytes`, opening it with `passphrase` on first sight.
    // Returns nullptr when ArchiveReader::open refuses the buffer; failures
    // are NOT cached (parity with the old per-call behaviour, and a wrong
    // password must stay re-tryable).
    [[nodiscard]] ArchiveReader* get(std::span<const uint8_t> bytes,
                                     std::string_view         passphrase);

    // Evict the reader for `bytes` (no-op when absent). Must be called
    // before the buffer dies — see the header comment.
    void drop(std::span<const uint8_t> bytes) noexcept;

    // Successful ArchiveReader::open calls made by this cache. The Phase 84
    // scaling observable: a full recursive import must cost O(archives)
    // opens, not O(entries).
    [[nodiscard]] size_t opens() const noexcept { return opens_; }

private:
    struct Key {
        const uint8_t* data;
        size_t         size;
        bool           operator==(const Key&) const = default;
    };
    struct KeyHash {
        [[nodiscard]] size_t operator()(const Key& k) const noexcept
        {
            return std::hash<const uint8_t*>{}(k.data) ^ (k.size << 1);
        }
    };

    std::unordered_map<Key, std::unique_ptr<ArchiveReader>, KeyHash> readers_;
    size_t                                                           opens_ = 0;
};

} // namespace ui

#endif // OSV_VENDORED_ARCHIVE
