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
#elif defined(_WIN32)
// _dup / _dup2 / _close / _fileno
#  include <io.h>
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
    // writing; the function must report failure rather than crash. Targets
    // the process's own stdin — a pre-existing global stream, exactly like
    // the real stdout/stderr this function targets in production — rather
    // than a freshly heap-allocated one: per POSIX, a stream's state after a
    // failed freopen() is undefined, so deciding whether to fclose() a fresh
    // allocation afterward is inherently unsafe (observed in practice: one
    // libc leaves it allocated, another frees it, depending on the platform).
    // Nothing else in this test binary reads from stdin.
    fs::path bad = fs::temp_directory_path() / "osv_no_such_dir_xyz" / "file.log";
    CHECK_FALSE(platform::redirect_stream_to_file(stdin, bad));
}

TEST(redirect_diagnostics_to_log_file_call)
{
    // No-op outside Windows Release; must be callable without crashing
    // everywhere (it's called unconditionally from App::init() in Release).
    //
    // On Windows it is NOT a no-op: it freopen()s this process's real stdout and
    // stderr onto config_dir()/console.log. Left that way it would swallow every
    // remaining line of the test run — including the FAIL line of any later test,
    // leaving CI with a bare "exit code 1" and no idea which test broke (that is
    // exactly what happened here). So duplicate both fds first and restore them
    // afterwards, keeping the redirect scoped to this test.
#if defined(_WIN32)
    const int saved_out = _dup(_fileno(stdout));
    const int saved_err = _dup(_fileno(stderr));
#endif

    platform::redirect_diagnostics_to_log_file();

#if defined(_WIN32)
    std::fflush(stdout);
    std::fflush(stderr);
    if (saved_out >= 0) {
        _dup2(saved_out, _fileno(stdout));
        _close(saved_out);
    }
    if (saved_err >= 0) {
        _dup2(saved_err, _fileno(stderr));
        _close(saved_err);
    }
    std::clearerr(stdout);
    std::clearerr(stderr);

    // If the restore silently failed, every later test's output is lost, so say
    // so now while this line can still be seen.
    CHECK_TRUE(saved_out >= 0 && saved_err >= 0);
#endif
}
