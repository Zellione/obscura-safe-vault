#include "harden.h"

#include "platform/safe_print.h"

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
        platform::safe_println(stderr, "[Platform] setrlimit(RLIMIT_CORE, 0) failed");
    }
    // Also attempt setrlimit on Linux for defense-in-depth (harmless if it fails).
#else
    // Linux: prefer prctl(PR_SET_DUMPABLE, 0) to prevent core dumps and ptrace attach.
    if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0) {
        platform::safe_println(stderr, "[Platform] prctl(PR_SET_DUMPABLE, 0) failed");
    }
    // Also use setrlimit for defense-in-depth.
    const struct rlimit zero_core{0, 0};
    if (setrlimit(RLIMIT_CORE, &zero_core) != 0) {
        platform::safe_println(stderr, "[Platform] setrlimit(RLIMIT_CORE, 0) failed");
    }
#endif
}

} // namespace platform
