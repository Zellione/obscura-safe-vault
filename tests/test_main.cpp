#include "test_framework.h"

#include "platform/locale_init.h"

#include <cstdio>
#include <print>

int main()
{
    // Same LC_CTYPE init as the app (src/app/main.cpp): the libarchive-backed
    // import tests decode CJK entry names, which the "C" locale cannot.
    platform::init_locale();

    // Unbuffer stdout so progress survives an abnormal exit. std::println is
    // block-buffered to a pipe (CI), so a crash mid-suite would otherwise lose
    // all buffered output, leaving a failure with no indication of which test
    // crashed (see Phase 15 PR1 — an MSVC Release miscompile did exactly this).
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::println("Running osv crypto tests...\n");
    return ::testing::run_all_tests();
}
