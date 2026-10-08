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
