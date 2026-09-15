#include "harden.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include "platform/safe_print.h"

#include "platform/paths.h"
#include "platform/path_utf8.h"

#include <sys/prctl.h>     // prctl
#include <sys/resource.h>  // setrlimit

namespace platform {

void disable_core_dumps() noexcept
{
    // Linux: prefer prctl(PR_SET_DUMPABLE, 0) to prevent core dumps and ptrace attach.
    if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0) {
        platform::safe_println(stderr, "[Platform] prctl(PR_SET_DUMPABLE, 0) failed");
    }
    // Also use setrlimit for defense-in-depth.
    const struct rlimit zero_core{0, 0};
    if (setrlimit(RLIMIT_CORE, &zero_core) != 0) {
        platform::safe_println(stderr, "[Platform] setrlimit(RLIMIT_CORE, 0) failed");
    }
}

bool redirect_stream_to_file(std::FILE* stream, const std::filesystem::path& path) noexcept
{
    // Binary mode: text mode on Windows translates '\n' to "\r\n", which
    // would make the on-disk line endings platform-dependent (mirrors
    // error_log.cpp / theme_pref.cpp's own file writes).
    return platform::freopen_path(path, "ab", stream) != nullptr;
}

void redirect_diagnostics_to_log_file() noexcept
{
    // POSIX logs go directly to stderr; nothing to redirect.
}

bool grow_secure_mem_budget(size_t bytes) noexcept
{
    // The soft RLIMIT_MEMLOCK may be raised to the hard limit without
    // privileges; anything beyond that needs ulimit/systemd configuration.
    struct rlimit rl{};
    if (getrlimit(RLIMIT_MEMLOCK, &rl) != 0) return false;
    if (rl.rlim_cur != RLIM_INFINITY &&
        (rl.rlim_max == RLIM_INFINITY || rl.rlim_cur < rl.rlim_max)) {
        rl.rlim_cur = rl.rlim_max;
        (void)setrlimit(RLIMIT_MEMLOCK, &rl);  // best-effort
        (void)getrlimit(RLIMIT_MEMLOCK, &rl);
    }
    return rl.rlim_cur == RLIM_INFINITY || rl.rlim_cur >= bytes;
}

size_t lockable_budget_bytes() noexcept
{
    struct rlimit rl{};
    if (getrlimit(RLIMIT_MEMLOCK, &rl) != 0) return 0;
    return rl.rlim_cur == RLIM_INFINITY ? SIZE_MAX : static_cast<size_t>(rl.rlim_cur);
}

} // namespace platform
