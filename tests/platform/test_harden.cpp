#include "test_framework.h"

#include "platform/harden.h"

#if defined(__linux__)
#  include <sys/prctl.h>
#  include <sys/resource.h>
#elif defined(__APPLE__)
#  include <sys/resource.h>
#endif

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
