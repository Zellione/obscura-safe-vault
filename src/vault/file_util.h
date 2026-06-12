#pragma once

// Small cross-platform stdio helpers for 64-bit positioning and durable flush.
// Shared by chunk_store (append/read) and vault (header writes). premake builds
// 64-bit only, so off_t / _ftelli64 are wide enough for any vault.

#include <cstdint>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#  include <io.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace vault::fileutil {

[[nodiscard]] inline bool seek_to(std::FILE* fp, uint64_t off) noexcept
{
#if defined(_WIN32)
    return _fseeki64(fp, static_cast<long long>(off), SEEK_SET) == 0;
#else
    return fseeko(fp, static_cast<off_t>(off), SEEK_SET) == 0;
#endif
}

[[nodiscard]] inline bool seek_end(std::FILE* fp, uint64_t& out_pos) noexcept
{
#if defined(_WIN32)
    if (_fseeki64(fp, 0, SEEK_END) != 0) return false;
    const long long p = _ftelli64(fp);
#else
    if (fseeko(fp, 0, SEEK_END) != 0) return false;
    const off_t p = ftello(fp);
#endif
    if (p < 0) return false;
    out_pos = static_cast<uint64_t>(p);
    return true;
}

[[nodiscard]] inline bool file_size(std::FILE* fp, uint64_t& out_size) noexcept
{
    return seek_end(fp, out_size);
}

// --- fault injection (crash-safety tests) ---------------------------------
// The double-buffered index swap is only crash-safe if an fsync failure at any
// step leaves a reopenable vault. There is no portable way to make a real
// fsync fail on demand, so tests arm this counter to make the Nth upcoming
// sync() call report failure (0 = the very next call). Disarmed by default and
// after firing; a single branch on the cold sync() path in production.

inline int& sync_fail_after() noexcept
{
    static int n = -1;
    return n;
}

inline void inject_sync_failure(int after_calls) noexcept { sync_fail_after() = after_calls; }
inline void clear_sync_failure() noexcept                 { sync_fail_after() = -1; }

// Flush stdio buffers and fsync to durable storage.
[[nodiscard]] inline bool sync(std::FILE* fp) noexcept
{
    if (int& n = sync_fail_after(); n >= 0) {
        if (n == 0) { n = -1; return false; }
        --n;
    }
    if (std::fflush(fp) != 0) return false;
#if defined(_WIN32)
    return _commit(_fileno(fp)) == 0;
#else
    return fsync(fileno(fp)) == 0;
#endif
}

// Best-effort fsync of the directory containing `path`, making a just-renamed
// file durable on POSIX (the rename itself lives in directory metadata).
// Windows has no directory handles to fsync this way; metadata durability is
// handled by the filesystem there.
inline void sync_dir_of(const std::string& path) noexcept
{
#if !defined(_WIN32)
    std::string dir = path;
    if (const auto slash = dir.find_last_of('/'); slash != std::string::npos) {
        dir.resize(slash == 0 ? 1 : slash);
    } else {
        dir = ".";
    }
    if (const int fd = ::open(dir.c_str(), O_RDONLY); fd >= 0) {
        (void)::fsync(fd);
        ::close(fd);
    }
#else
    (void)path;
#endif
}

} // namespace vault::fileutil
