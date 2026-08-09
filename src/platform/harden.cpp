#include "harden.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <print>

#include "platform/paths.h"
#include "platform/path_utf8.h"

#if defined(_WIN32)
// Get/SetProcessWorkingSetSize for grow_secure_mem_budget(). (Core dumps work
// differently on Windows; disable_core_dumps() needs nothing from here.)
#  include <windows.h>
#else
// Linux and other POSIX.
#  include <sys/prctl.h>     // prctl
#  include <sys/resource.h>  // setrlimit
#endif

namespace platform {

void disable_core_dumps() noexcept
{
#if defined(_WIN32)
    // Windows doesn't support prctl; core dumps are managed differently.
    // Nothing to do.
#else
    // Linux: prefer prctl(PR_SET_DUMPABLE, 0) to prevent core dumps and ptrace attach.
    if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0) {
        std::println(stderr, "[Platform] prctl(PR_SET_DUMPABLE, 0) failed");
    }
    // Also use setrlimit for defense-in-depth.
    const struct rlimit zero_core{0, 0};
    if (setrlimit(RLIMIT_CORE, &zero_core) != 0) {
        std::println(stderr, "[Platform] setrlimit(RLIMIT_CORE, 0) failed");
    }
#endif
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
#if defined(_WIN32)
    const std::filesystem::path dir = config_dir();
    if (dir.empty()) return;
    const std::filesystem::path log_path = dir / "console.log";
    redirect_stream_to_file(stdout, log_path);
    redirect_stream_to_file(stderr, log_path);
#endif
}

bool grow_secure_mem_budget(size_t bytes) noexcept
{
#if defined(_WIN32)
    // VirtualLock's per-process cap is the minimum working-set size minus a
    // few pages of overhead, so the minimum must cover `bytes` plus slack for
    // the app's own resident pages. Never shrink what the process already has.
    HANDLE proc    = GetCurrentProcess();
    SIZE_T cur_min = 0;
    SIZE_T cur_max = 0;
    if (GetProcessWorkingSetSize(proc, &cur_min, &cur_max) == 0) return false;

    constexpr SIZE_T slack    = SIZE_T{16} << 20;
    const SIZE_T     want_min = std::max(cur_min, static_cast<SIZE_T>(bytes) + slack);
    const SIZE_T     want_max = std::max(cur_max, want_min + slack);
    if (want_min == cur_min) return true;  // already big enough

    if (SetProcessWorkingSetSize(proc, want_min, want_max) == 0) {
        std::println(stderr, "[Platform] SetProcessWorkingSetSize({} MiB) failed",
                     want_min >> 20);
        return false;
    }
    return true;
#else
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
#endif
}

} // namespace platform
