#include "test_framework.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "platform/safe_print.h"

namespace fs = std::filesystem;

namespace {
std::string read_text(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
} // namespace

TEST(safe_println_writes_formatted_text_to_a_writable_stream)
{
    fs::path p = fs::temp_directory_path() / "osv_safe_print_write.tmp";
    std::error_code ec;
    fs::remove(p, ec);

    std::FILE* f = std::fopen(p.string().c_str(), "w");
    REQUIRE(f != nullptr);
    platform::safe_println(f, "[Tag] value={}", 42);
    std::fclose(f);

    CHECK_EQ(read_text(p), std::string("[Tag] value=42\n"));
    fs::remove(p, ec);
}

TEST(safe_println_does_not_throw_when_the_stream_cannot_be_written_to)
{
    // A stream opened read-only guarantees a write failure without relying on
    // UB (e.g. closing the fd behind a live FILE*). std::println throws
    // std::system_error on such a failure; safe_println must swallow it —
    // this is exactly the Windows-Release-WindowedApp scenario where stderr
    // has no valid handle at all ("bad file descriptor"), which previously
    // took down the whole process via std::terminate.
    fs::path p = fs::temp_directory_path() / "osv_safe_print_readonly.tmp";
    { std::ofstream(p) << "x"; }

    std::FILE* f = std::fopen(p.string().c_str(), "r");
    REQUIRE(f != nullptr);
    platform::safe_println(f, "should not throw {}", 1); // must not throw/crash
    std::fclose(f);

    std::error_code ec;
    fs::remove(p, ec);
}
