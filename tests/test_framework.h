#pragma once

// Tiny no-dependency, no-exceptions test harness.
//
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); ... }
//
// Tests self-register via a static initializer. main() (test_main.cpp) calls
// run_all_tests(), which runs every registered test and returns non-zero if any
// check failed. REQUIRE aborts the current test on failure (skips the rest of
// its body); CHECK records the failure and continues.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <source_location>
#include <span>
#include <string_view>
#include <vector>

namespace testing {

struct TestCase {
    const char* name;
    std::function<void(int&)> fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

// Helper functions for source_location-aware checking
inline void check_failed(bool passed, std::string_view expr, int& failures,
                          std::source_location loc) noexcept
{
    if (!passed) {
        std::printf("    check failed: %.*s  (%s:%u)\n",
                    static_cast<int>(expr.size()), expr.data(),
                    loc.file_name(), loc.line());
        ++failures;
    }
}

inline void check_eq_failed(bool passed, std::string_view lhs, std::string_view rhs,
                             int& failures,
                             std::source_location loc) noexcept
{
    if (!passed) {
        std::printf("    check failed: %.*s == %.*s  (%s:%u)\n",
                    static_cast<int>(lhs.size()), lhs.data(),
                    static_cast<int>(rhs.size()), rhs.data(),
                    loc.file_name(), loc.line());
        ++failures;
    }
}

inline void check_bytes_failed(bool passed, std::string_view lhs, std::string_view rhs,
                                int& failures,
                                std::source_location loc) noexcept
{
    if (!passed) {
        std::printf("    check failed: bytes %.*s == %.*s  (%s:%u)\n",
                    static_cast<int>(lhs.size()), lhs.data(),
                    static_cast<int>(rhs.size()), rhs.data(),
                    loc.file_name(), loc.line());
        ++failures;
    }
}

inline void require_failed(bool passed, std::string_view expr,
                            int& failures,
                            std::source_location loc) noexcept
{
    if (!passed) {
        std::printf("    REQUIRE failed: %.*s  (%s:%u)\n",
                    static_cast<int>(expr.size()), expr.data(),
                    loc.file_name(), loc.line());
        ++failures;
    }
}

struct Registrar {
    Registrar(const char* name, std::function<void(int&)> fn) { registry().push_back({name, std::move(fn)}); }
};

// Compare two byte ranges for equality.
inline bool bytes_equal(std::span<const uint8_t> a, std::span<const uint8_t> b)
{
    if (a.size() != b.size()) return false;
    if (a.empty()) return true;  // memcmp(nullptr, nullptr, 0) is UB; short-circuit
    return std::memcmp(a.data(), b.data(), a.size()) == 0;
}

inline int run_all_tests()
{
    int total_failures = 0;
    int failed_tests   = 0;
    for (const auto& tc : registry()) {
        int failures = 0;
        tc.fn(failures);
        if (failures == 0) {
            std::printf("  PASS  %s\n", tc.name);
        } else {
            std::printf("  FAIL  %s (%d check%s failed)\n",
                        tc.name, failures, failures == 1 ? "" : "s");
            ++failed_tests;
        }
        total_failures += failures;
    }
    std::printf("\n%zu tests, %d failed (%d total check failures)\n",
                registry().size(), failed_tests, total_failures);
    return failed_tests == 0 ? 0 : 1;
}

} // namespace testing

// --- Test definition ------------------------------------------------------
// Each TEST body receives a hidden `int& _failures` it can increment.
#define TEST(name)                                                            \
    static void test_##name(int& _failures);                                  \
    static ::testing::Registrar registrar_##name(#name, &test_##name);        \
    static void test_##name([[maybe_unused]] int& _failures)

// --- Assertion macros -----------------------------------------------------
#define CHECK(cond)  \
    ::testing::check_failed((cond), #cond, _failures, std::source_location::current())

#define CHECK_TRUE(cond)   CHECK(cond)
#define CHECK_FALSE(cond)  ::testing::check_failed(!(cond), #cond, _failures, std::source_location::current())

#define CHECK_EQ(a, b)  \
    ::testing::check_eq_failed((a) == (b), #a, #b, _failures, std::source_location::current())

#define CHECK_BYTES_EQ(a, b)  \
    ::testing::check_bytes_failed(::testing::bytes_equal((a), (b)), #a, #b, _failures, std::source_location::current())

// REQUIRE: on failure, record and return from the test immediately.
#define REQUIRE(cond)                                                               \
    do {                                                                            \
        if (!(cond)) {                                                              \
            ::testing::require_failed(false, #cond, _failures, std::source_location::current()); \
            return;                                                                 \
        }                                                                           \
    } while (0)
