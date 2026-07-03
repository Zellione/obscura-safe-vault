#include "test_framework.h"

#include "platform/harden.h"

#if defined(__linux__)
#  include <sys/prctl.h>
#endif

// Test that disable_core_dumps() can be called without crashing.
// On Linux, also verify that prctl(PR_GET_DUMPABLE) returns 0 after the call.
TEST(disable_core_dumps_call)
{
    // The function should not crash or throw.
    platform::disable_core_dumps();

#if defined(__linux__)
    // On Linux, verify that PR_GET_DUMPABLE is now 0 (dumps disabled).
    int dumpable = prctl(PR_GET_DUMPABLE);
    CHECK_EQ(dumpable, 0);
#endif
}
