#pragma once

// Opt-in performance tracing (Phase 58). Set OSV_PERF_LOG=1 and any PerfScope
// whose lifetime exceeds its threshold logs "[Perf] <label> <ms> ms" to stderr.
// Labels are static operation names ONLY — never node names, paths, or any
// vault-derived string (security invariant #5).

#include <chrono>
#include <string>

namespace platform {

// True when the OSV_PERF_LOG environment variable is set to a non-empty,
// non-"0" value. Read once and memoised (getenv is not thread-safe to poll).
[[nodiscard]] bool perf_log_enabled() noexcept;

// "[Perf] <label> <ms rounded to 0.1> ms" — pure, unit-tested.
[[nodiscard]] std::string perf_line(const char* label, double ms);

class PerfScope {
public:
    explicit PerfScope(const char* label, double threshold_ms = 10.0) noexcept;
    ~PerfScope();

    PerfScope(const PerfScope&)            = delete;
    PerfScope& operator=(const PerfScope&) = delete;
    PerfScope(PerfScope&&)                 = delete;
    PerfScope& operator=(PerfScope&&)      = delete;

private:
    const char*                           label_;
    double                                threshold_ms_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace platform
