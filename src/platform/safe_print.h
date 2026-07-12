#pragma once

#include <cstdio>
#include <print>
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
// this wrapper instead, so a failed diagnostic print can never itself crash.
template <class... Args>
void safe_println(std::FILE* stream, std::format_string<Args...> fmt, Args&&... args) noexcept
{
    try {
        std::println(stream, fmt, std::forward<Args>(args)...);
    } catch (...) {
        // Deliberately swallowed: a failed diagnostic print must never crash.
    }
}

} // namespace platform
