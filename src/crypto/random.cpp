#include "random.h"

#include <cstdio>

#if defined(_WIN32)
#  include <windows.h>
#  include <bcrypt.h>
#  pragma comment(lib, "bcrypt.lib")
#elif defined(__APPLE__)
#  include <sys/random.h>   // getentropy
#else
#  include <cerrno>
#  include <sys/random.h>   // getrandom
#  include <cstdio>         // /dev/urandom fallback
#endif

namespace crypto {

#if defined(_WIN32)

bool fill_random(std::span<uint8_t> out) noexcept
{
    NTSTATUS s = BCryptGenRandom(
        nullptr, out.data(), static_cast<ULONG>(out.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (s != 0) {
        std::fprintf(stderr, "[crypto] BCryptGenRandom failed (0x%lx)\n",
                     static_cast<unsigned long>(s));
        return false;
    }
    return true;
}

#elif defined(__APPLE__)

bool fill_random(std::span<uint8_t> out) noexcept
{
    // getentropy is limited to 256 bytes per call.
    size_t off = 0;
    while (off < out.size()) {
        size_t chunk = out.size() - off;
        if (chunk > 256) chunk = 256;
        if (getentropy(out.data() + off, chunk) != 0) {
            std::fprintf(stderr, "[crypto] getentropy failed\n");
            return false;
        }
        off += chunk;
    }
    return true;
}

#else // Linux / other POSIX with getrandom

static bool fill_from_urandom(std::span<uint8_t> out) noexcept
{
    std::FILE* f = std::fopen("/dev/urandom", "rb");
    if (!f) {
        std::fprintf(stderr, "[crypto] cannot open /dev/urandom\n");
        return false;
    }
    size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (got != out.size()) {
        std::fprintf(stderr, "[crypto] short read from /dev/urandom\n");
        return false;
    }
    return true;
}

bool fill_random(std::span<uint8_t> out) noexcept
{
    size_t off = 0;
    while (off < out.size()) {
        ssize_t n = getrandom(out.data() + off, out.size() - off, 0);
        if (n < 0) {
            if (errno == EINTR) continue;          // interrupted, retry
            if (errno == ENOSYS) {                 // kernel too old: fall back
                return fill_from_urandom(out.subspan(off));
            }
            std::fprintf(stderr, "[crypto] getrandom failed (errno=%d)\n", errno);
            return false;
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

#endif

} // namespace crypto
