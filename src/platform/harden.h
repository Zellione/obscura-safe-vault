#pragma once

#include <cstdio>
#include <filesystem>

// Platform hardening: core-dump protection, mlock advice, etc.

namespace platform {

// Disable core dumps on this process. Called once at app startup (Release builds only)
// to prevent core dumps from containing decrypted data or key material.
//
// On Linux: uses prctl(PR_SET_DUMPABLE, 0) to prevent ptrace attach and core dumps.
// On macOS: uses setrlimit(RLIMIT_CORE, {0,0}) to disable core dumps.
// On Windows: no-op (Windows doesn't support prctl and core dumps work differently).
//
// Logs a [Platform] error line if it fails; silent on success.
// Note: This is only called in Release builds (NDEBUG defined). Debug builds keep
// core dumps + ptrace attach enabled so developers can run debuggers and analyze
// crashes. The tradeoff is intentional: dev machines with proper access control
// can be trusted; deployed apps need the stronger guarantee.
void disable_core_dumps() noexcept;

// Redirects `stream`'s underlying OS handle to `path` (append mode), so every
// subsequent write to `stream` goes there instead. Returns false without
// crashing if `path` cannot be opened (in which case `stream` is left in
// whatever state the platform's freopen() failure leaves it in — callers
// should not write to it further). Exposed as a pure function of an explicit
// FILE* + path (not the process's real stdout/stderr) so it's unit-testable.
bool redirect_stream_to_file(std::FILE* stream, const std::filesystem::path& path) noexcept;

// Redirects the process's stdout/stderr to config_dir()/"console.log"
// (Windows Release builds only; no-op elsewhere). A Release build runs as a
// windowless (WindowedApp) subsystem process, so stdout/stderr start with no
// valid OS handle at all — every write to them fails ("bad file
// descriptor"), and C++23's std::print/std::println throw std::system_error
// on such a failure (unlike old fprintf), which would crash the whole
// process via std::terminate() the first time any of the many existing
// std::println(stderr, "[Module] ...") diagnostics ran. Redirecting to a log
// file (rather than discarding to the null device) also means those
// diagnostics — previously invisible in a windowless build — become visible
// for the first time. Call once, early, at app startup (before any
// diagnostic print).
void redirect_diagnostics_to_log_file() noexcept;

} // namespace platform
