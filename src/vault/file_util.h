#pragma once

// Small cross-platform stdio helpers for 64-bit positioning and durable flush.
// Shared by chunk_store (append/read) and vault (header writes). premake builds
// 64-bit only, so off_t / _ftelli64 are wide enough for any vault.

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
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

// Same arm-once pattern for rename: compaction's atomic commit is a rename,
// and its failure-recovery path (reacquire the original handle) is otherwise
// untestable — there is no portable way to make a real rename fail on demand.
inline int& rename_fail_after() noexcept
{
    static int n = -1;
    return n;
}

inline void inject_rename_failure(int after_calls) noexcept { rename_fail_after() = after_calls; }
inline void clear_rename_failure() noexcept                 { rename_fail_after() = -1; }

// Rename `from` over `to`. Returns false on failure (injected or real).
[[nodiscard]] inline bool rename_file(const std::string& from, const std::string& to) noexcept
{
    if (int& n = rename_fail_after(); n >= 0) {
        if (n == 0) { n = -1; return false; }
        --n;
    }
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    return !ec;
}

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

// Best-effort secure wipe: overwrite a file's contents with zeros in chunks,
// then remove it. Failures are logged but non-fatal; the file is removed
// regardless. Works on both POSIX and Windows.
// NOTE: This is a best-effort wipe. Copy-on-write filesystems (btrfs, APFS),
// SSD wear-leveling, and snapshots may retain old blocks regardless of
// overwriting. This helper mitigates forensic recovery for typical
// filesystems only.
inline void wipe_and_remove(const std::string& path) noexcept
{
    // Attempt to open and overwrite the file with zeros in chunks.
    const std::string p = path;
    std::FILE* fp = std::fopen(p.c_str(), "r+b");
    if (fp) {
        constexpr size_t WIPE_CHUNK = 1024 * 1024;  // 1 MiB chunks
        std::array<uint8_t, WIPE_CHUNK> zeros{};
        uint64_t remaining = 0;
        if (seek_end(fp, remaining)) {
            if (seek_to(fp, 0)) {
                while (remaining > 0) {
                    const size_t to_write = std::min(static_cast<size_t>(remaining), WIPE_CHUNK);
                    if (std::fwrite(zeros.data(), 1, to_write, fp) != to_write) {
                        break;  // write failed; remove what we have
                    }
                    remaining -= to_write;
                }
                // Best-effort fsync the wipe.
                (void)sync(fp);
            }
        }
        std::fclose(fp);
    }
    // Remove the file regardless of wipe success (non-fatal).
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace vault::fileutil
