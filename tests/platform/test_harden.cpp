#include "test_framework.h"

#include "platform/harden.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(__linux__)
#  include <sys/prctl.h>
#  include <sys/resource.h>
#elif defined(__APPLE__)
#  include <sys/resource.h>
#endif

namespace fs = std::filesystem;

// Test that disable_core_dumps() can be called without crashing.
// On Linux, also verify that prctl(PR_GET_DUMPABLE) returns 0 after the call.
TEST(disable_core_dumps_call)
{
    // Capture original state for restoration (test isolation).
#if defined(__linux__) || defined(__APPLE__)
    struct rlimit old_limit;
    getrlimit(RLIMIT_CORE, &old_limit);
#endif

    // The function should not crash or throw.
    platform::disable_core_dumps();

#if defined(__linux__)
    // On Linux, verify that PR_GET_DUMPABLE is now 0 (dumps disabled).
    int dumpable = prctl(PR_GET_DUMPABLE);
    CHECK_EQ(dumpable, 0);
    // restore: keep the shared test process dumpable for later tests
    (void)prctl(PR_SET_DUMPABLE, 1);
#endif

#if defined(__linux__) || defined(__APPLE__)
    // Restore original rlimit.
    setrlimit(RLIMIT_CORE, &old_limit);
#endif
}

TEST(redirect_stream_to_file_succeeds_and_writes_land_in_the_file)
{
    fs::path p = fs::temp_directory_path() / "osv_redirect_stream.log";
    std::error_code ec;
    fs::remove(p, ec);

    std::FILE* f = std::tmpfile();
    REQUIRE(f != nullptr);

    CHECK_TRUE(platform::redirect_stream_to_file(f, p));
    std::fputs("hello\n", f);
    std::fclose(f);

    std::ifstream in(p, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK_EQ(content, std::string("hello\n"));
    fs::remove(p, ec);
}

TEST(redirect_stream_to_file_returns_false_for_an_unopenable_path)
{
    // A path inside a directory that doesn't exist can never be opened for
    // writing; the function must report failure rather than crash.
    fs::path bad = fs::temp_directory_path() / "osv_no_such_dir_xyz" / "file.log";

    std::FILE* f = std::tmpfile();
    REQUIRE(f != nullptr);

    CHECK_FALSE(platform::redirect_stream_to_file(f, bad));
    // Per freopen(), the stream's state on failure is platform-defined —
    // don't write to or fclose it further.
}

TEST(redirect_diagnostics_to_log_file_call)
{
    // No-op outside Windows Release; must be callable without crashing
    // everywhere (it's called unconditionally from App::init() in Release).
    platform::redirect_diagnostics_to_log_file();
}
