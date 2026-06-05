#pragma once

// SecureBuffer<N> — a fixed-size byte buffer for key material.
//
// Security invariants (CLAUDE.md):
//   * mlock'd on construction so the bytes never swap to disk.
//   * crypto_wipe'd on destruction so freed memory holds no key material.
//
// mlock can legitimately fail on systems with a low RLIMIT_MEMLOCK. That is
// logged once and treated as non-fatal (the buffer is still wiped on destruct);
// the alternative — refusing to run — is worse for usability and we still get
// the wipe guarantee. is_locked() exposes the outcome for diagnostics/tests.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <utility>

#include <monocypher.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <sys/mman.h>
#endif

namespace crypto {

namespace detail {

// Best-effort page-lock. Returns true on success. Never throws.
inline bool mem_lock(void* p, size_t n) noexcept
{
#if defined(_WIN32)
    return VirtualLock(p, n) != 0;
#else
    return ::mlock(p, n) == 0;
#endif
}

inline void mem_unlock(void* p, size_t n) noexcept
{
#if defined(_WIN32)
    VirtualUnlock(p, n);
#else
    ::munlock(p, n);
#endif
}

} // namespace detail

template <size_t N>
class SecureBuffer {
    static_assert(N > 0, "SecureBuffer size must be non-zero");

public:
    SecureBuffer() noexcept
    {
        locked_ = detail::mem_lock(bytes_.data(), bytes_.size());
        if (!locked_) {
            // Non-fatal: we still wipe on destruction. Warn once per buffer.
            std::fprintf(stderr,
                "[crypto] mlock failed for %zu-byte secure buffer; "
                "key material may be swappable (raise RLIMIT_MEMLOCK)\n", N);
        }
    }

    ~SecureBuffer()
    {
        crypto_wipe(bytes_.data(), bytes_.size());
        if (locked_) {
            detail::mem_unlock(bytes_.data(), bytes_.size());
        }
    }

    // Non-copyable: duplicating key material is a footgun.
    SecureBuffer(const SecureBuffer&)            = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;

    // Movable: copy the bytes into a freshly-locked buffer, then wipe the source.
    SecureBuffer(SecureBuffer&& other) noexcept : SecureBuffer()
    {
        bytes_ = other.bytes_;
        crypto_wipe(other.bytes_.data(), other.bytes_.size());
    }

    SecureBuffer& operator=(SecureBuffer&& other) noexcept
    {
        if (this != &other) {
            bytes_ = other.bytes_;
            crypto_wipe(other.bytes_.data(), other.bytes_.size());
        }
        return *this;
    }

    [[nodiscard]] uint8_t*       data()       noexcept { return bytes_.data(); }
    [[nodiscard]] const uint8_t* data() const noexcept { return bytes_.data(); }
    [[nodiscard]] static constexpr size_t size() noexcept { return N; }

    [[nodiscard]] std::span<uint8_t, N>       span()       noexcept { return std::span<uint8_t, N>(bytes_); }
    [[nodiscard]] std::span<const uint8_t, N> as_span() const noexcept { return std::span<const uint8_t, N>(bytes_); }

    [[nodiscard]] bool is_locked() const noexcept { return locked_; }

    // Wipe now (idempotent; destruction wipes again harmlessly).
    void wipe() noexcept { crypto_wipe(bytes_.data(), bytes_.size()); }

private:
    std::array<uint8_t, N> bytes_{};
    bool                   locked_ = false;
};

} // namespace crypto
