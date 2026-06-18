#include "test_framework.h"

#include <cstdio>
#include <print>

int main()
{
    // Unbuffer stdout so progress survives an abnormal exit. std::println is
    // block-buffered to a pipe (CI), so a crash mid-suite would otherwise lose
    // all buffered output, leaving a failure with no indication of which test
    // crashed (see Phase 15 PR1 — an MSVC Release miscompile did exactly this).
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::println("Running osv crypto tests...\n");
    return ::testing::run_all_tests();
}
