#pragma once

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

} // namespace platform
