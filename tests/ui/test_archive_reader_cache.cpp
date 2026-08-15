// Phase 84: one open ArchiveReader per live archive buffer. Before this
// cache, the recursive-import hooks opened (and fully re-scanned) a fresh
// reader for EVERY extracted entry — the PR #124 O(n^2) class reintroduced
// one layer up. Keyed on (data pointer, size): safe only because the walker
// drops an archive's entry before its buffer dies (Task 2 wires that).

#ifdef OSV_VENDORED_ARCHIVE

#include "test_framework.h"
#include "ui/archive_reader_cache.h"
#include "archive_test_helpers.h"
#include "crypto/secure_mem.h"

#include <vector>

TEST(archive_reader_cache_reuses_one_reader_per_buffer)
{
    const auto path = archivetest::make_archive({{"a.jpg", archivetest::fake_bytes(1)},
                                                {"b.jpg", archivetest::fake_bytes(2)},
                                                {"c.jpg", archivetest::fake_bytes(3)}},
                                               "7zip", archivetest::fresh_path("cache_reuse.7z"));
    const std::vector<uint8_t> bytes = archivetest::read_file(path);
    REQUIRE(!bytes.empty());

    ui::ArchiveReaderCache cache;
    ui::ArchiveReader* r1 = cache.get(bytes, {});
    REQUIRE(r1 != nullptr);
    ui::ArchiveReader* r2 = cache.get(bytes, {});
    CHECK_EQ(r1, r2);                 // same buffer -> same reader, no reopen
    CHECK_EQ(cache.opens(), 1u);

    // Ascending extracts through the cached reader ride the forward cursor:
    // the whole pass costs ONE stream open inside the reader.
    crypto::SecureBytes out;
    for (size_t i = 0; i < r1->entries().size(); ++i) {
        REQUIRE(r1->extract(i, out));
    }
    CHECK_EQ(r1->stream_opens(), 1u);
}

TEST(archive_reader_cache_drop_evicts)
{
    const auto path = archivetest::make_archive({{"a.jpg", archivetest::fake_bytes(1)}},
                                               "7zip", archivetest::fresh_path("cache_drop.7z"));
    const std::vector<uint8_t> bytes = archivetest::read_file(path);
    REQUIRE(!bytes.empty());

    ui::ArchiveReaderCache cache;
    REQUIRE(cache.get(bytes, {}) != nullptr);
    cache.drop(bytes);
    REQUIRE(cache.get(bytes, {}) != nullptr);   // re-opens after eviction
    CHECK_EQ(cache.opens(), 2u);
}

TEST(archive_reader_cache_open_failure_not_cached)
{
    const std::vector<uint8_t> junk{0xDE, 0xAD, 0xBE, 0xEF};
    ui::ArchiveReaderCache cache;
    CHECK(cache.get(junk, {}) == nullptr);
    CHECK(cache.get(junk, {}) == nullptr);      // still refused, no crash
    CHECK_EQ(cache.opens(), 0u);
}

#endif  // OSV_VENDORED_ARCHIVE
