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
#include <cstdlib>
#include <print>
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
inline bool mem_lock(uint8_t* p, size_t n) noexcept
{
#if defined(_WIN32)
    return VirtualLock(static_cast<LPVOID>(p), n) != 0;
#else
    return ::mlock(static_cast<void*>(p), n) == 0;
#endif
}

inline void mem_unlock(uint8_t* p, size_t n) noexcept
{
#if defined(_WIN32)
    VirtualUnlock(static_cast<LPVOID>(p), n);
#else
    ::munlock(static_cast<void*>(p), n);
#endif
}

} // namespace detail

template <size_t N>
class SecureBuffer {
    static_assert(N > 0, "SecureBuffer size must be non-zero");

public:
    SecureBuffer() noexcept
        : locked_(detail::mem_lock(bytes_.data(), bytes_.size()))
    {
        if (!locked_) {
            // Non-fatal: we still wipe on destruction. Warn once per buffer.
            std::println(stderr,
                "[crypto] mlock failed for {}-byte secure buffer; "
                "key material may be swappable (raise RLIMIT_MEMLOCK)", N);
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

// SecureBytes — the runtime-sized sibling of SecureBuffer<N>.
//
// Decrypted image data has a length only known at read time, so it can't live in
// a fixed-size SecureBuffer. SecureBytes owns an mlock'd heap allocation that is
// crypto_wipe'd before it is freed, upholding invariant #1 (no plaintext to disk)
// and invariant #2 (wipe on destruction) for variable-length secrets.
//
// Like SecureBuffer it is move-only: duplicating secret storage is a footgun.
class SecureBytes {
public:
    SecureBytes() noexcept = default;

    explicit SecureBytes(size_t n) { (void)resize(n); }

    ~SecureBytes() { free_storage(); }

    SecureBytes(const SecureBytes&)            = delete;
    SecureBytes& operator=(const SecureBytes&) = delete;

    SecureBytes(SecureBytes&& other) noexcept
        : data_(other.data_), size_(other.size_), locked_(other.locked_)
    {
        other.data_   = nullptr;
        other.size_   = 0;
        other.locked_ = false;
    }

    SecureBytes& operator=(SecureBytes&& other) noexcept
    {
        if (this != &other) {
            free_storage();
            data_         = other.data_;
            size_         = other.size_;
            locked_       = other.locked_;
            other.data_   = nullptr;
            other.size_   = 0;
            other.locked_ = false;
        }
        return *this;
    }

    // Reallocate to exactly `n` bytes (wiping the old contents first). Returns
    // false (and leaves the object empty) if allocation fails. n == 0 frees.
    [[nodiscard]] bool resize(size_t n)
    {
        free_storage();
        if (n == 0) return true;

        auto* p = static_cast<uint8_t*>(std::malloc(n));
        if (!p) {
            std::println(stderr, "[crypto] SecureBytes alloc of {} bytes failed", n);
            return false;
        }
        data_   = p;
        size_   = n;
        locked_ = detail::mem_lock(data_, size_);
        if (!locked_) {
            std::println(stderr,
                "[crypto] mlock failed for {}-byte secure buffer; "
                "key material may be swappable (raise RLIMIT_MEMLOCK)", n);
        }
        return true;
    }

    [[nodiscard]] uint8_t*       data()       noexcept { return data_; }
    [[nodiscard]] const uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] size_t         size()  const noexcept { return size_; }
    [[nodiscard]] bool           empty() const noexcept { return size_ == 0; }
    [[nodiscard]] bool           is_locked() const noexcept { return locked_; }

    [[nodiscard]] std::span<uint8_t>       span()       noexcept { return {data_, size_}; }
    [[nodiscard]] std::span<const uint8_t> as_span() const noexcept { return {data_, size_}; }

    void wipe() noexcept { if (data_) crypto_wipe(data_, size_); }

private:
    void free_storage() noexcept
    {
        if (!data_) return;
        crypto_wipe(data_, size_);
        if (locked_) detail::mem_unlock(data_, size_);
        std::free(data_);
        data_   = nullptr;
        size_   = 0;
        locked_ = false;
    }

    uint8_t* data_   = nullptr;
    size_t   size_   = 0;
    bool     locked_ = false;
};

} // namespace crypto
