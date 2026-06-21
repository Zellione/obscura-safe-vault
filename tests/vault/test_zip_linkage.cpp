#include "test_framework.h"
#include "miniz.h"

#include <cstring>

// Proves the miniz static lib is linked and the actual ZIP reader+writer API
// (the one Tasks 2-3 depend on) is reachable: write a one-entry archive in
// memory, then read the entry back out byte-for-byte. Consumers include the
// umbrella "miniz.h" — miniz_zip.h alone lacks mz_alloc_func / MZ_BEST_SPEED.
TEST(miniz_zip_round_trip)
{
    const char payload[] = "obscura";

    mz_zip_archive w; std::memset(&w, 0, sizeof(w));
    REQUIRE(mz_zip_writer_init_heap(&w, 0, 0));
    REQUIRE(mz_zip_writer_add_mem(&w, "a.txt", payload, sizeof(payload) - 1, MZ_BEST_SPEED));
    void* buf = nullptr; size_t sz = 0;
    REQUIRE(mz_zip_writer_finalize_heap_archive(&w, &buf, &sz));
    mz_zip_writer_end(&w);

    mz_zip_archive r; std::memset(&r, 0, sizeof(r));
    REQUIRE(mz_zip_reader_init_mem(&r, buf, sz, 0));
    REQUIRE(mz_zip_reader_get_num_files(&r) == 1);
    mz_zip_archive_file_stat st;
    REQUIRE(mz_zip_reader_file_stat(&r, 0, &st));
    CHECK(std::strcmp(st.m_filename, "a.txt") == 0);
    CHECK(st.m_uncomp_size == sizeof(payload) - 1);

    char out[16] = {};
    REQUIRE(mz_zip_reader_extract_to_mem(&r, 0, out, sizeof(payload) - 1, 0));
    CHECK(std::memcmp(out, payload, sizeof(payload) - 1) == 0);
    mz_zip_reader_end(&r);
    mz_free(buf);
}
