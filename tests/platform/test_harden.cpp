#include "test_framework.h"

#include "crypto/secure_mem.h"
#include "platform/harden.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

// Core files must stay disabled without making the process non-dumpable.
// xdg-desktop-portal resolves native dialog paths through /proc/<pid>/root;
// PR_SET_DUMPABLE=0 denies that same-user lookup and breaks every Release
// file dialog before the callback can return a path.
TEST(disable_core_dumps_preserves_portal_access)
{
    // Lowering the hard limit to zero cannot be undone without privilege, so
    // isolate this process-wide hardening operation in a child.
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        const int old_dumpable = prctl(PR_GET_DUMPABLE);
        platform::disable_core_dumps();

        struct rlimit disabled_limit{};
        const bool ok = old_dumpable >= 0 && getrlimit(RLIMIT_CORE, &disabled_limit) == 0 &&
                        disabled_limit.rlim_cur == 0 && disabled_limit.rlim_max == 0 &&
                        prctl(PR_GET_DUMPABLE) == old_dumpable;
        _exit(ok ? 0 : 1);
    }

    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    CHECK_TRUE(WIFEXITED(status));
    CHECK_EQ(WEXITSTATUS(status), 0);
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
    // allocation afterward is inherently unsafe.
    // Nothing else in this test binary reads from stdin.
    fs::path bad = fs::temp_directory_path() / "osv_no_such_dir_xyz" / "file.log";
    CHECK_FALSE(platform::redirect_stream_to_file(stdin, bad));
}

TEST(redirect_diagnostics_to_log_file_call)
{
    // POSIX logs go directly to stderr; the function is a no-op and must be
    // callable without crashing (called unconditionally from App::init()).
    platform::redirect_diagnostics_to_log_file();
}

// grow_secure_mem_budget() must report success for a small request: 1 MiB is
// below the Linux RLIMIT_MEMLOCK default (8 MiB).
TEST(grow_secure_mem_budget_small_request_succeeds)
{
    CHECK_TRUE(platform::grow_secure_mem_budget(1u << 20));
}

// Phase 6c: the UI shows the page-lock budget the user actually has; it must
// be a positive, reportable value on every supported platform (this box's
// RLIMIT_MEMLOCK default is 8 MiB; CI's is similar).
TEST(lockable_budget_bytes_is_positive)
{
    CHECK(platform::lockable_budget_bytes() > 0);
}

// On Linux the call raises the soft RLIMIT_MEMLOCK to the hard limit (the
// most an unprivileged process may lock without configuration changes).
TEST(grow_secure_mem_budget_raises_soft_memlock_to_hard)
{
    REQUIRE(platform::grow_secure_mem_budget(1));
    struct rlimit rl{};
    REQUIRE(getrlimit(RLIMIT_MEMLOCK, &rl) == 0);
    CHECK_TRUE(rl.rlim_cur == rl.rlim_max);
}
