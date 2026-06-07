#include "random.h"

#include <print>

#if defined(_WIN32)
#  include <windows.h>
#  include <bcrypt.h>
#  pragma comment(lib, "bcrypt.lib")
#elif defined(__APPLE__)
#  include <sys/random.h>   // getentropy
#else
#  include <cerrno>
#  include <cstdio>
#  include <sys/random.h>   // getrandom
#endif

namespace crypto {

#if defined(_WIN32)

bool fill_random(std::span<uint8_t> out) noexcept
{
    NTSTATUS s = BCryptGenRandom(
        nullptr, out.data(), static_cast<ULONG>(out.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (s != 0) {
        std::println(stderr, "[crypto] BCryptGenRandom failed (0x{:lx})",
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
            std::println(stderr, "[crypto] getentropy failed");
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
        std::println(stderr, "[crypto] cannot open /dev/urandom");
        return false;
    }
    size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (got != out.size()) {
        std::println(stderr, "[crypto] short read from /dev/urandom");
        return false;
    }
    return true;
}

bool fill_random(std::span<uint8_t> out) noexcept
{
    size_t off = 0;
    while (off < out.size()) {
        if (ssize_t n = getrandom(out.data() + off, out.size() - off, 0); n < 0) {
            if (errno == EINTR) continue;          // interrupted, retry
            if (errno == ENOSYS) {                 // kernel too old: fall back
                return fill_from_urandom(out.subspan(off));
            }
            std::println(stderr, "[crypto] getrandom failed (errno={})", errno);
            return false;
        } else {
            off += static_cast<size_t>(n);
        }
    }
    return true;
}

#endif

} // namespace crypto
