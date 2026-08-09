#pragma once

// Small cross-platform stdio helpers for 64-bit positioning and durable flush.
// Shared by chunk_store (append/read) and vault (header writes). premake builds
// 64-bit only, so off_t / _ftelli64 are wide enough for any vault.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <sys/stat.h>

#if defined(_WIN32)
#  include <io.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#  if defined(__linux__)
#    include <linux/falloc.h>  // FALLOC_FL_PUNCH_HOLE / FALLOC_FL_KEEP_SIZE
#  endif
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

// Report the file size WITHOUT moving the stream position. This must never be
// seek-based: seek_end mutates the shared FILE*'s position, and a concurrent
// size query (e.g. Vault::wasted_bytes on the main thread) landing between a
// writer's seek_to(0) and its fwrite in write_header would redirect that header
// write to end-of-file, silently persisting a stale index. fstat reads the size
// from the fd's metadata and is position-independent. (Flake fix: the commit
// lane's header swap raced main-thread file_size calls on the same handle.)
[[nodiscard]] inline bool file_size(std::FILE* fp, uint64_t& out_size) noexcept
{
#if defined(_WIN32)
    struct _stat64 st{};
    if (_fstat64(_fileno(fp), &st) != 0 || st.st_size < 0) return false;
#else
    struct stat st{};
    if (::fstat(::fileno(fp), &st) != 0 || st.st_size < 0) return false;
#endif
    out_size = static_cast<uint64_t>(st.st_size);
    return true;
}

// Report the file's ALLOCATED size (physical blocks actually on disk), which
// differs from file_size() once a file is sparse. Used to measure how much disk
// in-place hole-punching actually reclaimed. POSIX: st_blocks is in 512-byte
// units by definition. Windows has no cheap portable equivalent, so it falls
// back to the logical size (hole-punching is a no-op there anyway).
[[nodiscard]] inline bool file_allocated_bytes(std::FILE* fp, uint64_t& out_size) noexcept
{
#if defined(_WIN32)
    return file_size(fp, out_size);
#else
    struct stat st{};
    if (::fstat(::fileno(fp), &st) != 0 || st.st_blocks < 0) {
        return false;
    }
    out_size = static_cast<uint64_t>(st.st_blocks) * 512U;
    return true;
#endif
}

// Deallocate the file blocks backing [offset, offset+len), leaving a hole: the
// range reads back as zeros and the freed blocks return to the filesystem, but
// the file's logical size is unchanged (FALLOC_FL_KEEP_SIZE), so every byte
// offset outside the hole stays put. This is how the vault reclaims orphaned
// chunk space IN PLACE — no temp copy, no offset rewrite, so no transient
// doubling of disk use. Only whole filesystem blocks fully inside the range are
// freed; partial blocks at the edges are merely zeroed, never the live data
// outside the range. Returns false (a harmless no-op) on platforms or
// filesystems without hole-punch support. Linux only for now.
[[nodiscard]] inline bool punch_hole(std::FILE* fp, uint64_t offset, uint64_t len) noexcept
{
    if (len == 0) {
        return false;
    }
#if defined(__linux__)
    if (std::fflush(fp) != 0) {  // flush stdio so the fd sees the bytes
        return false;
    }
    return ::fallocate(::fileno(fp), FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                       static_cast<off_t>(offset), static_cast<off_t>(len)) == 0;
#else
    (void)fp;
    (void)offset;
    return false;
#endif
}

// Truncate the file to exactly `new_size` bytes (flushing stdio first so no
// buffered write lands past the new end). Used by in-place compaction to cut
// the dead tail after the final index commit. The stream position is left
// unspecified — callers seek before the next read/write.
[[nodiscard]] inline bool truncate_file(std::FILE* fp, uint64_t new_size) noexcept
{
    if (std::fflush(fp) != 0) return false;
#if defined(_WIN32)
    return _chsize_s(_fileno(fp), static_cast<long long>(new_size)) == 0;
#else
    return ::ftruncate(::fileno(fp), static_cast<off_t>(new_size)) == 0;
#endif
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

// Total sync() calls this process (atomic: the commit lane syncs off-thread).
// Tests reset it and assert bulk operations batch their durable commits
// instead of paying one per file; a plain counter, never used for control flow.
inline std::atomic<uint64_t>& sync_call_count() noexcept
{
    static std::atomic<uint64_t> n{0};
    return n;
}

// Flush stdio buffers and fsync to durable storage.
[[nodiscard]] inline bool sync(std::FILE* fp) noexcept
{
    sync_call_count().fetch_add(1, std::memory_order_relaxed);
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

} // namespace vault::fileutil
