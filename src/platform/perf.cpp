#include "platform/perf.h"

#include <cstdio>
#include <cstdlib>
#include <format>

#include "platform/safe_print.h"

namespace platform {

bool perf_log_enabled() noexcept
{
    static const bool enabled = [] {
        const char* v = std::getenv("OSV_PERF_LOG");
        return v != nullptr && v[0] != '\0' && !(v[0] == '0' && v[1] == '\0');
    }();
    return enabled;
}

std::string perf_line(const char* label, double ms)
{
    return std::format("[Perf] {} {:.1f} ms", label, ms);
}

PerfScope::PerfScope(const char* label, double threshold_ms) noexcept
    : label_(label), threshold_ms_(threshold_ms)
{
}

PerfScope::~PerfScope()
{
    if (!perf_log_enabled()) return;
    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start_).count();
    if (ms < threshold_ms_) return;
    safe_println(stderr, "{}", perf_line(label_, ms));
}

} // namespace platform
