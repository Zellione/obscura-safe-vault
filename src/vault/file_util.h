#pragma once

// Small cross-platform stdio helpers for 64-bit positioning and durable flush.
// Shared by chunk_store (append/read) and vault (header writes). premake builds
// 64-bit only, so off_t / _ftelli64 are wide enough for any vault.

#include <cstdint>
#include <cstdio>

#if defined(_WIN32)
#  include <io.h>
#else
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

// Flush stdio buffers and fsync to durable storage.
[[nodiscard]] inline bool sync(std::FILE* fp) noexcept
{
    if (std::fflush(fp) != 0) return false;
#if defined(_WIN32)
    return _commit(_fileno(fp)) == 0;
#else
    return fsync(fileno(fp)) == 0;
#endif
}

} // namespace vault::fileutil
