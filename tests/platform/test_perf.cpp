#include "platform/perf.h"

#include "test_framework.h"

TEST(perf_line_formats_label_and_ms)
{
    CHECK_EQ(platform::perf_line("grid.refresh", 12.34), "[Perf] grid.refresh 12.3 ms");
    CHECK_EQ(platform::perf_line("frame", 250.0), "[Perf] frame 250.0 ms");
}

TEST(perf_scope_without_env_is_inert)
{
    // OSV_PERF_LOG is not set in the test environment: constructing and
    // destroying a scope must be side-effect-free (and must not crash).
    for (int i = 0; i < 3; ++i) {
        platform::PerfScope scope("test.noop", 0.0);
    }
    CHECK(!platform::perf_log_enabled());
}
