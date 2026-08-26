#pragma once

// SecureBuffer<N> — a fixed-size byte buffer for key material.
//
// Security invariants (AGENTS.md):
//   * mlock'd on construction so the bytes never swap to disk.
//   * crypto_wipe'd on destruction so freed memory holds no key material.
//
// mlock can legitimately fail on systems with a low RLIMIT_MEMLOCK. That is
// logged once and treated as non-fatal (the buffer is still wiped on destruct);
// the alternative — refusing to run — is worse for usability and we still get
// the wipe guarantee. is_locked() exposes the outcome for diagnostics/tests.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include "platform/safe_print.h"
#include <span>
#include <utility>

#include <monocypher.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <sys/mman.h>
#endif

namespace crypto {

// Process-wide record that at least one mlock/VirtualLock has failed this
// process. should_warn_mlock_once() flips it (returning true exactly once, to
// log the warning); mlock_failure_seen() reports it, so the UI can surface
// that some decoded data is sitting in swappable memory (Phase 6c).
// The flag lives as a function-local static behind this accessor rather than a
// namespace-scope global (cpp:S5421). Semantics are unchanged: the inline
// function's static is a single program-wide instance, initialized on first
// use (thread-safe, immune to cross-translation-unit init order).
[[nodiscard]] inline std::atomic_bool& mlock_failed_flag() noexcept
{
    static std::atomic_bool flag{false};
    return flag;
}

// Thread-safe helper: should we warn about the first mlock failure?
// Returns true exactly once per process; all subsequent calls return false.
// Used by both SecureBuffer and SecureBytes to log a prominent warning on the
// first mlock failure, then stay silent on subsequent failures.
inline bool should_warn_mlock_once() noexcept
{
    bool expected = false;
    return mlock_failed_flag().compare_exchange_strong(expected, true);
}

// True once any mlock/VirtualLock has failed (i.e. some secret buffer degraded
// to swappable memory). Monotonic: never goes back to false in a process.
[[nodiscard]] inline bool mlock_failure_seen() noexcept
{
    return mlock_failed_flag().load();
}

// Platform-appropriate remedy advice for the once-per-process mlock warning.
// On Windows the cap is the process's minimum working-set size (VirtualLock),
// not RLIMIT_MEMLOCK, so ulimit advice would be meaningless there. Startup
// grows the budget via platform::grow_secure_mem_budget() (harden.h); a
// failure after that means the machine is under real memory pressure.
inline const char* mlock_fail_hint() noexcept
{
#if defined(_WIN32)
    return "process working set too small — startup grows it via "
           "SetProcessWorkingSetSize, so the system is likely low on memory";
#else
    return "RLIMIT_MEMLOCK too low? Raise with: ulimit -l / systemd LimitMEMLOCK";
#endif
}

// Body of the once-per-process mlock warning. Split from the once-gate so the
// failure path stays testable: CI never fails an mlock, so this line would
// otherwise only ever run on an end-user machine.
inline void print_mlock_warning() noexcept
{
    platform::safe_println(stderr, "[SecureMem] WARNING: mlock failed — decoded data may be swappable ({}).",
                 mlock_fail_hint());
}

// Non-fatal degrade: the buffer still wipes on destruction. Warn once process-wide.
inline void warn_mlock_failure_once() noexcept
{
    if (should_warn_mlock_once()) print_mlock_warning();
}

namespace detail {

// Best-effort page-lock. Returns true on success. Never throws.
// On Linux, after a successful mlock, also attempts madvise(MADV_DONTDUMP)
// for defense-in-depth (harmless if it fails).
inline bool mem_lock(uint8_t* p, size_t n) noexcept
{
#if defined(_WIN32)
    return VirtualLock(p, n) != 0;
#else
    if (::mlock(p, n) != 0) return false;

    // Defense-in-depth: mark the page as not dumpable (Linux only).
    // This prevents the page from being included in core dumps even if the
    // process is still dumpable. Ignore failures silently.
#  ifdef __linux__
    (void)::madvise(p, n, MADV_DONTDUMP);
#  endif
    return true;
#endif
}

inline void mem_unlock(uint8_t* p, size_t n) noexcept
{
#if defined(_WIN32)
    // NOTE: S995 (const param): VirtualUnlock takes LPVOID (void*, not const void*),
    // so the param cannot be const without a const_cast. POSIX munlock accepts const void*,
    // but Windows does not. We keep the param non-const to avoid const_cast. This is
    // acceptable as S859 (API compatibility) outranks S995 (const-correctness).
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
        : locked_(detail::mem_lock(bytes_.data(), bytes_.size()))
    {
        if (!locked_) warn_mlock_failure_once();
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
    SecureBuffer(SecureBuffer&& other) noexcept
        : bytes_(other.bytes_)
        , locked_(detail::mem_lock(bytes_.data(), bytes_.size()))
    {
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
        : data_(std::move(other.data_)), size_(other.size_), locked_(other.locked_)
    {
        other.size_   = 0;
        other.locked_ = false;
    }

    SecureBytes& operator=(SecureBytes&& other) noexcept
    {
        if (this != &other) {
            free_storage();
            data_         = std::move(other.data_);
            size_         = other.size_;
            locked_       = other.locked_;
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

        try {
            data_ = std::make_unique<uint8_t[]>(n);
        } catch (const std::bad_alloc&) {
            platform::safe_println(stderr, "[crypto] SecureBytes alloc of {} bytes failed", n);
            return false;
        }
        size_   = n;
        locked_ = detail::mem_lock(data_.get(), size_);
        if (!locked_) warn_mlock_failure_once();
        return true;
    }

    [[nodiscard]] uint8_t*       data()       noexcept { return data_.get(); }
    [[nodiscard]] const uint8_t* data() const noexcept { return data_.get(); }
    [[nodiscard]] size_t         size()  const noexcept { return size_; }
    [[nodiscard]] bool           empty() const noexcept { return size_ == 0; }
    [[nodiscard]] bool           is_locked() const noexcept { return locked_; }

    // Unchecked element access — callers own the bounds (the buffer is sized
    // exactly for its contents, e.g. decoded pixels are always w*h*3 bytes).
    [[nodiscard]] uint8_t& operator[](size_t i) noexcept { return data_[i]; }
    [[nodiscard]] const uint8_t& operator[](size_t i) const noexcept { return data_[i]; }

    // Copy from an external buffer, resizing (and wiping) first. Returns false
    // — leaving the object empty — if the span is null with non-zero size or
    // the allocation fails.
    [[nodiscard]] bool assign(std::span<const uint8_t> src)
    {
        if (src.size() && !src.data()) return false;
        if (!resize(src.size())) return false;
        std::copy_n(src.data(), src.size(), data_.get());
        return true;
    }

    // Resize to n and fill with v (solid-colour buffers). False on allocation
    // failure, leaving the object empty.
    [[nodiscard]] bool fill(size_t n, uint8_t v)
    {
        if (!resize(n)) return false;
        std::fill_n(data_.get(), n, v);
        return true;
    }

    [[nodiscard]] std::span<uint8_t>       span()       noexcept { return {data_.get(), size_}; }
    [[nodiscard]] std::span<const uint8_t> as_span() const noexcept { return {data_.get(), size_}; }

    void wipe() noexcept { if (data_) crypto_wipe(data_.get(), size_); }

private:
    void free_storage() noexcept
    {
        if (!data_) return;
        crypto_wipe(data_.get(), size_);
        if (locked_) detail::mem_unlock(data_.get(), size_);
        data_.reset();
        size_   = 0;
        locked_ = false;
    }

    std::unique_ptr<uint8_t[]> data_;
    size_t                     size_   = 0;
    bool                       locked_ = false;
};

} // namespace crypto
