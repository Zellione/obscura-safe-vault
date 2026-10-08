#include "platform/safe_print.h"
#pragma once

#include <cstddef>
#include <cstdio>
#include <filesystem>

// Platform hardening: core-dump protection, mlock advice, etc.

namespace platform {

// Disable core dumps on this process. Called once at app startup (Release builds only)
// to prevent core dumps from containing decrypted data or key material.
//
// On Linux: uses setrlimit(RLIMIT_CORE, {0,0}). PR_SET_DUMPABLE must remain
// unchanged because xdg-desktop-portal needs same-user access to
// /proc/<pid>/root when opening native file dialogs.
//
// Logs a [Platform] error line if it fails; silent on success.
// Note: This is only called in Release builds (NDEBUG defined). Debug builds keep
// core dumps enabled so developers can analyze crashes.
void disable_core_dumps() noexcept;

// Best-effort: grow the amount of memory this process may page-lock so the
// SecureBuffer/SecureBytes mlock calls (crypto/secure_mem.h) can
// actually succeed for decoded-image-sized buffers. Returns true when the
// platform reports a lockable budget of at least `bytes` afterwards.
//
// Linux: raise the soft RLIMIT_MEMLOCK to the hard limit (allowed without
// privileges); anything beyond the hard limit needs ulimit/systemd config.
//
// Called once at app startup (all build configs). Failure is non-fatal:
// secure_mem.h keeps its warn-once + degrade-to-swappable behaviour.
bool grow_secure_mem_budget(size_t bytes) noexcept;

// The page-lockable budget this process currently has, in bytes.
//
// Linux: the soft RLIMIT_MEMLOCK (the cap on mlock). Reported as
// SIZE_MAX when the kernel says it is unlimited (RLIM_INFINITY).
//
// Returns 0 if the platform cannot report it. The UI uses this to show the
// user how much memory can actually be held out of swap (Phase 6c).
[[nodiscard]] size_t lockable_budget_bytes() noexcept;

} // namespace platform
