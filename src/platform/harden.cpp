#include "harden.h"

#include <cstdio>
#include <filesystem>
#include <print>

#include "platform/paths.h"

#if defined(_WIN32)
// Windows: core dumps work differently; no action needed.
#elif defined(__APPLE__)
#  include <sys/resource.h>  // setrlimit
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
#elif defined(__APPLE__)
    // macOS: setrlimit(RLIMIT_CORE, {0, 0}) disables core dumps.
    const struct rlimit zero_core{0, 0};
    if (setrlimit(RLIMIT_CORE, &zero_core) != 0) {
        std::println(stderr, "[Platform] setrlimit(RLIMIT_CORE, 0) failed");
    }
    // Also attempt setrlimit on Linux for defense-in-depth (harmless if it fails).
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
    return std::freopen(path.string().c_str(), "a", stream) != nullptr;
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

} // namespace platform
