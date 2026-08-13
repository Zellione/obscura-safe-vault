#pragma once

#include <cstdio>
#include <format>
#include <string>
#include <utility>

namespace platform {

// std::println throws std::system_error if the underlying write fails. On
// Windows, a Release build runs as a windowless (WindowedApp) subsystem
// process with no console attached, so stdout/stderr have no valid OS handle
// and every write to them fails ("bad file descriptor") — an uncaught throw
// there crashes the whole process via std::terminate before any diagnostic
// is ever seen (this took down the app even inside error_log.cpp's own
// terminate handler, which logged via a raw std::println(stderr, ...) call).
// Every std::println(stream, ...) call site in this codebase must go through
// this wrapper instead, so a failed diagnostic print can never itself crash:
// it formats into a std::string and writes via std::fputs, which reports a
// dead stream through its return value instead of throwing.
template <class... Args>
void safe_println(std::FILE* stream, std::format_string<Args...> fmt, Args&&... args) noexcept
{
    std::string line;
    try {
        line = std::format(fmt, std::forward<Args>(args)...);
        line.push_back('\n');
    } catch (const std::exception&) {
        // std::format can still throw (std::bad_alloc, or std::format_error
        // for a runtime-bad argument). Emit an unformatted marker so the
        // failure is at least visible on a live stream; on a dead stream
        // fputs fails via its return value, never by throwing.
        (void)std::fputs("[safe_println] failed to format a diagnostic\n", stream);
        return;
    }
    (void)std::fputs(line.c_str(), stream);
}

} // namespace platform
