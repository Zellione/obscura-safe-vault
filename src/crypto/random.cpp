#include "random.h"

#include "platform/safe_print.h"

#if defined(__APPLE__)
#  error "macOS is no longer supported (Linux only since Phase 101)"
#else
#  include <cerrno>
#  include <cstdio>
#  include <sys/random.h>   // getrandom
#endif

namespace crypto {

static bool fill_from_urandom(std::span<uint8_t> out) noexcept
{
    std::FILE* f = std::fopen("/dev/urandom", "rb");
    if (!f) {
        platform::safe_println(stderr, "[crypto] cannot open /dev/urandom");
        return false;
    }
    size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (got != out.size()) {
        platform::safe_println(stderr, "[crypto] short read from /dev/urandom");
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
            platform::safe_println(stderr, "[crypto] getrandom failed (errno={})", errno);
            return false;
        } else {
            off += static_cast<size_t>(n);
        }
    }
    return true;
}

} // namespace crypto
