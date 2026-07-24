// Guards the invariant behind a real durability flake: fileutil::file_size must
// report the size WITHOUT moving the stream position. When it was implemented as
// seek_end, a main-thread size query (Vault::wasted_bytes) could move the shared
// FILE*'s position between the commit lane's seek_to(0) and its fwrite in
// write_header, redirecting the header write to end-of-file and persisting a
// stale index — intermittently losing committed data under load.

#include "test_framework.h"

#include "vault/file_util.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace vault;

namespace {

fs::path tmp_file(const char* name)
{
    return fs::temp_directory_path() / name;
}

}  // namespace

TEST(file_size_does_not_move_the_stream_position)
{
    const auto path = tmp_file("osv_file_size_pos.bin");
    std::FILE* fp = std::fopen(path.string().c_str(), "w+b");
    REQUIRE(fp != nullptr);
    const std::vector<uint8_t> payload(4096, 0xAB);
    REQUIRE(std::fwrite(payload.data(), 1, payload.size(), fp) == payload.size());
    REQUIRE(std::fflush(fp) == 0);

    // Park the position somewhere specific, then query the size.
    REQUIRE(fileutil::seek_to(fp, 100));
    uint64_t size = 0;
    REQUIRE(fileutil::file_size(fp, size));
    CHECK_EQ(size, static_cast<uint64_t>(4096));

    // The position must be exactly where we left it — file_size is a pure query.
    const long pos = std::ftell(fp);
    CHECK_EQ(pos, 100L);

    std::fclose(fp);
    fs::remove(path);
}

TEST(file_size_concurrent_with_seek0_writes_never_misplaces_them)
{
    // Reproduces the exact race shape: one thread does seek_to(0)+fwrite (like
    // write_header), another hammers file_size (like wasted_bytes). With a
    // position-independent file_size, every header write lands at offset 0 and
    // the file never grows past HEADER.
    const auto path = tmp_file("osv_file_size_race.bin");
    std::FILE* fp = std::fopen(path.string().c_str(), "w+b");
    REQUIRE(fp != nullptr);
    constexpr size_t HDR = 4096;
    std::vector<uint8_t> hdr(HDR, 0);
    REQUIRE(std::fwrite(hdr.data(), 1, HDR, fp) == HDR);
    REQUIRE(std::fflush(fp) == 0);

    std::atomic<bool> stop{false};
    std::thread reader([&] {
        uint64_t sz = 0;
        while (!stop.load()) {
            (void)fileutil::file_size(fp, sz);
        }
    });

    uint64_t misplaced = 0;
    for (uint64_t i = 0; i < 100000; ++i) {
        REQUIRE(fileutil::seek_to(fp, 0));
        std::this_thread::yield();  // widen the seek(0)->write gap for the reader
        if (std::ftell(fp) != 0) {
            ++misplaced;
        }
        std::memset(hdr.data(), static_cast<int>(i & 0xFF), HDR);
        REQUIRE(std::fwrite(hdr.data(), 1, HDR, fp) == HDR);
    }
    stop.store(true);
    reader.join();
    REQUIRE(std::fflush(fp) == 0);

    uint64_t final_size = 0;
    REQUIRE(fileutil::file_size(fp, final_size));
    std::fclose(fp);
    fs::remove(path);

    CHECK_EQ(misplaced, static_cast<uint64_t>(0));
    CHECK_EQ(final_size, static_cast<uint64_t>(HDR));  // never grew => no misplaced header
}
