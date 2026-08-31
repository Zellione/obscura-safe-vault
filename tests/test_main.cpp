#include "test_framework.h"

#include "platform/harden.h"
#include "platform/locale_init.h"

#include <cstdio>
#include <print>

int main(int argc, char** argv)
{
    // Same LC_CTYPE init as the app (src/app/main.cpp): the libarchive-backed
    // import tests decode CJK entry names, which the "C" locale cannot.
    platform::init_locale();

    // Match App::init(): Windows VirtualLock is capped by the process minimum
    // working set, so secure-buffer tests need the production lock budget too.
    constexpr size_t SECURE_MEM_BUDGET = size_t{256} << 20;
    (void)platform::grow_secure_mem_budget(SECURE_MEM_BUDGET);

    // Unbuffer stdout so progress survives an abnormal exit. std::println is
    // block-buffered to a pipe (CI), so a crash mid-suite would otherwise lose
    // all buffered output, leaving a failure with no indication of which test
    // crashed (see Phase 15 PR1 — an MSVC Release miscompile did exactly this).
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::println("Running osv crypto tests...\n");
    // Optional argv[1]: substring filter, e.g. `osv_tests probe_video` — used to
    // point valgrind/gdb at a single test.
    return ::testing::run_all_tests(argc > 1 ? argv[1] : nullptr);
}
