// src/platform/atomic_file.h
#pragma once

// Phase 98 (security audit Phase E / OSV-AUD-005): atomic, link-safe creation
// of brand-new output files. This is the ONLY sanctioned way to write an export
// sink: it closes the check-then-truncating-open race in the old export path,
// where a `fopen(path, "wb")` could follow a symlink/reparse point swapped in
// AFTER the path had been validated and collision-probed.
//
// Contract of create_new_file_within:
//   * ATOMIC collision handling — the exclusive create itself is the existence
//     test, so there is no window in which another process's file gets
//     truncated (O_EXCL / CREATE_NEW are the arbiter, not a preceding stat).
//   * NO symlink following — RESOLVE_NO_SYMLINKS + O_NOFOLLOW / final-handle
//     reparse containment; a link at the candidate name simply counts as a
//     collision and is suffixed past.
//   * CONTAINMENT — creation happens relative to an open directory (Linux
//     openat2 RESOLVE_BENEATH / openat on a dirfd; Windows CREATE_NEW then a
//     GetFinalPathNameByHandleW prefix check), so a swap of the directory name
//     itself cannot redirect the write, and `safe_component` must be one plain
//     filename (the helper rejects any separator / "." / ".." / NUL up front).
//   * The caller receives an already-open handle it writes into and then drops
//     (NewOutputFile RAII closes). It NEVER reopens the display_path — a path
//     that was merely checked earlier is exactly the race this module removes.
//
// SDL-free and headless-testable (like path_utf8.h), so the platform/ layering
// exception applies: ui/export.h and the test binary both include this header.

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>

namespace platform {

// An exclusively-created, already-open output file plus the path it took.
//
// move-only: ownership of `fp` transfers with the object and the destructor
// closes it (`display_path` is informational only — logs / status / the
// post-create containment assertion — and is deliberately NEVER reopened).
struct NewOutputFile {
    NewOutputFile() = default;
    NewOutputFile(std::FILE* f, std::filesystem::path p)
        : fp(f), display_path(std::move(p))
    {
    }
    ~NewOutputFile()
    {
        if (fp) std::fclose(fp);
    }
    NewOutputFile(NewOutputFile&& o) noexcept
        : fp(std::exchange(o.fp, nullptr)), display_path(std::move(o.display_path))
    {
    }
    NewOutputFile& operator=(NewOutputFile&& o) noexcept
    {
        if (this != &o) {
            if (fp) std::fclose(fp);
            fp = std::exchange(o.fp, nullptr);
            display_path = std::move(o.display_path);
        }
        return *this;
    }
    NewOutputFile(const NewOutputFile&) = delete;
    NewOutputFile& operator=(const NewOutputFile&) = delete;

    std::FILE* fp = nullptr;
    std::filesystem::path display_path;
};

// Atomically claim a brand-new file strictly inside `directory`. `safe_component`
// must already be a safe single filename (vault::is_safe_node_name output); the
// helper defensively rejects anything carrying a path separator, "." / "..", a
// NUL, or exceeding 255 bytes. On collision with an EXISTING entry the name is
// suffixed "name (n).ext" (n = 1, 2, ... — the Phase 10 naming) until a free
// name is claimed atomically. Returns nullopt and leaves NO file behind on any
// failure (invalid component / unopenable directory / permission denied / I/O
// error / an exhausted collision streak).
//
// The returned handle is write-ready ("wb"). May throw std::bad_alloc on memory
// exhaustion (callers on the worker thread treat it as fatal, matching every
// other allocation path in this codebase).
[[nodiscard]] std::optional<NewOutputFile>
create_new_file_within(const std::filesystem::path& directory,
                       std::string_view safe_component);

// TEST SEAM (certain races can't be provoked deterministically): arm the next
// create to report the candidate as already taken WITHOUT creating anything —
// the attacker-winning-the-window case the module is designed to survive. The
// helper must then suffix and continue, truncating nothing. Disarmed after one
// use; never armed by production code.
void inject_atomic_create_collision() noexcept;
void clear_atomic_create_collision() noexcept;

}  // namespace platform