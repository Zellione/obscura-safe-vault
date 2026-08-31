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
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include "platform/safe_print.h"
#include <span>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <monocypher.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
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

// FFmpeg and some codec/driver APIs retain internal plaintext allocations for
// which no caller-supplied allocator or final-release callback exists. Track
// that technically unavoidable boundary separately from a real mlock failure,
// then fold both into the status shown in F1.
[[nodiscard]] inline std::atomic_bool& opaque_plaintext_flag() noexcept
{
    static std::atomic_bool flag{false};
    return flag;
}

inline void mark_opaque_plaintext_seen() noexcept
{
    opaque_plaintext_flag().store(true);
}

[[nodiscard]] inline bool opaque_plaintext_seen() noexcept
{
    return opaque_plaintext_flag().load();
}

[[nodiscard]] inline bool secure_memory_degraded() noexcept
{
    return mlock_failure_seen() || opaque_plaintext_seen();
}

inline void clear_opaque_plaintext_seen_for_tests() noexcept
{
    opaque_plaintext_flag().store(false);
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

inline size_t memory_page_size() noexcept
{
#if defined(_WIN32)
    static const size_t page_size = [] {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        return static_cast<size_t>(info.dwPageSize);
    }();
#else
    static const size_t page_size = [] {
        const long n = ::sysconf(_SC_PAGESIZE);
        return n > 0 ? static_cast<size_t>(n) : static_cast<size_t>(4096);
    }();
#endif
    return page_size;
}

struct PageLockRegistry {
    std::mutex mu;
    std::unordered_map<uintptr_t, size_t> refs;
};

inline PageLockRegistry& page_lock_registry()
{
    static PageLockRegistry registry;
    return registry;
}

template <typename T>
inline uintptr_t pointer_address(const T* p) noexcept
{
    return std::bit_cast<uintptr_t>(p);
}

inline uintptr_t page_base(uintptr_t address) noexcept
{
    const size_t page_size = memory_page_size();
    return address / page_size * page_size;
}

inline bool os_lock_range(uintptr_t first, size_t length) noexcept
{
    auto* p = std::bit_cast<std::byte*>(first);
#if defined(_WIN32)
    return VirtualLock(p, length) != 0;
#else
    if (::mlock(p, length) != 0) return false;

    // Defense-in-depth: mark the page as not dumpable (Linux only).
    // This prevents the page from being included in core dumps even if the
    // process is still dumpable. Ignore failures silently.
#  ifdef __linux__
    (void)::madvise(p, length, MADV_DONTDUMP);
#  endif
    return true;
#endif
}

inline void os_unlock_range(uintptr_t first, size_t length) noexcept
{
#if defined(_WIN32)
    auto* p = std::bit_cast<std::byte*>(first);
    (void)VirtualUnlock(p, length);
#else
    const auto* p = std::bit_cast<const std::byte*>(first);
    (void)::munlock(p, length);
#endif
}

struct AddedPageRefs {
    bool success;
    bool added_page;
};

inline void rollback_page_refs(PageLockRegistry& registry, uintptr_t first, uintptr_t stop,
                               size_t page_size) noexcept
{
    for (uintptr_t page = first; page != stop; page += page_size) {
        const auto it = registry.refs.find(page);
        if (it == registry.refs.end()) continue;
        --it->second;
        if (it->second == 0) registry.refs.erase(it);
    }
}

inline bool add_page_ref(PageLockRegistry& registry, uintptr_t page, bool& added_page) noexcept
{
    if (const auto it = registry.refs.find(page); it != registry.refs.end()) {
        ++it->second;
        return true;
    }
    try {
        registry.refs.emplace(page, 1);
        added_page = true;
        return true;
    } catch (...) {
        return false;
    }
}

inline AddedPageRefs add_page_refs(PageLockRegistry& registry, uintptr_t first, uintptr_t last,
                                   size_t page_size) noexcept
{
    bool added_page = false;
    for (uintptr_t page = first;; page += page_size) {
        if (!add_page_ref(registry, page, added_page)) {
            rollback_page_refs(registry, first, page, page_size);
            return {.success = false, .added_page = false};
        }
        if (page == last) break;
    }
    return {.success = true, .added_page = added_page};
}

inline bool is_new_page(const PageLockRegistry& registry, uintptr_t page) noexcept
{
    if (const auto it = registry.refs.find(page); it != registry.refs.end())
        return it->second == 1;
    return false;
}

inline void unlock_new_pages_before(const PageLockRegistry& registry, uintptr_t first,
                                    uintptr_t stop, size_t page_size) noexcept
{
    uintptr_t range_first = 0;
    for (uintptr_t page = first; page != stop; page += page_size) {
        if (is_new_page(registry, page)) {
            if (range_first == 0) range_first = page;
        } else if (range_first != 0) {
            os_unlock_range(range_first, static_cast<size_t>(page - range_first));
            range_first = 0;
        }
    }
    if (range_first != 0)
        os_unlock_range(range_first, static_cast<size_t>(stop - range_first));
}

inline bool lock_new_pages(const PageLockRegistry& registry, uintptr_t first, uintptr_t last,
                           size_t page_size) noexcept
{
    uintptr_t range_first = 0;
    for (uintptr_t page = first;; page += page_size) {
        if (is_new_page(registry, page)) {
            if (range_first == 0) range_first = page;
        } else if (range_first != 0) {
            if (!os_lock_range(range_first, static_cast<size_t>(page - range_first))) {
                unlock_new_pages_before(registry, first, range_first, page_size);
                return false;
            }
            range_first = 0;
        }
        if (page == last) break;
    }
    if (range_first == 0) return true;
    if (os_lock_range(range_first, static_cast<size_t>(last - range_first) + page_size))
        return true;
    unlock_new_pages_before(registry, first, range_first, page_size);
    return false;
}

inline void release_page_refs(PageLockRegistry& registry, uintptr_t first, uintptr_t last,
                              size_t page_size) noexcept
{
    uintptr_t range_first = 0;
    for (uintptr_t page = first;; page += page_size) {
        bool released = false;
        if (const auto it = registry.refs.find(page); it != registry.refs.end()) {
            --it->second;
            if (it->second == 0) {
                registry.refs.erase(it);
                released = true;
            }
        }
        if (released) {
            if (range_first == 0) range_first = page;
        } else if (range_first != 0) {
            os_unlock_range(range_first, static_cast<size_t>(page - range_first));
            range_first = 0;
        }
        if (page == last) break;
    }
    if (range_first != 0)
        os_unlock_range(range_first, static_cast<size_t>(last - range_first) + page_size);
}

inline void forget_page_refs(PageLockRegistry& registry, uintptr_t first, uintptr_t last,
                             size_t page_size) noexcept
{
    for (uintptr_t page = first;; page += page_size) {
        if (const auto it = registry.refs.find(page); it != registry.refs.end()) {
            --it->second;
            if (it->second == 0) registry.refs.erase(it);
        }
        if (page == last) break;
    }
}

// mlock/munlock operate on whole pages and Linux locks do not stack. Normal
// allocator blocks share pages, so a per-allocation munlock can otherwise make
// a still-live neighbouring secret swappable. Keep one OS lock per page and a
// process-wide reference count across every SecureBuffer/SecureBytes/String.
template <typename T> inline bool mem_lock(const T* p, size_t n) noexcept
{
    if (!p || n == 0) return false;
    const auto addr = pointer_address(p);
    if (addr > std::numeric_limits<uintptr_t>::max() - (n - 1)) return false;
    const size_t page_size = memory_page_size();
    const uintptr_t first = page_base(addr);
    const uintptr_t last = page_base(addr + n - 1);
    if (last > std::numeric_limits<uintptr_t>::max() - page_size) return false;

    try {
        auto& registry = page_lock_registry();
        std::lock_guard lk(registry.mu);
        const AddedPageRefs added = add_page_refs(registry, first, last, page_size);
        if (!added.success) return false;
        if (!added.added_page) return true;
        if (!lock_new_pages(registry, first, last, page_size)) {
            rollback_page_refs(registry, first, last + page_size, page_size);
            return false;
        }
        return true;
    } catch (const std::system_error&) {
        return false;
    }
}

template <typename T> inline void mem_unlock(const T* p, size_t n) noexcept
{
    if (!p || n == 0) return;
    const auto addr = pointer_address(p);
    if (addr > std::numeric_limits<uintptr_t>::max() - (n - 1)) return;
    const size_t page_size = memory_page_size();
    const uintptr_t first = page_base(addr);
    const uintptr_t last = page_base(addr + n - 1);

    try {
        auto& registry = page_lock_registry();
        std::lock_guard lk(registry.mu);
        release_page_refs(registry, first, last, page_size);
    } catch (const std::system_error& error) {
        // Destructors cannot report failure to their caller, but the failure
        // must remain visible rather than silently leaving pages registered.
        platform::safe_println(stderr, "[SecureMem] WARNING: page unlock failed: {}",
                               error.what());
    }
}

// Drop registry ownership without touching the virtual address. This is only
// for an allocator that may already have realloc'd/freed the registered block:
// munlock/VirtualUnlock on that stale address could affect unrelated storage
// subsequently mapped at the same location.
template <typename T> inline void mem_forget_lock(const T* p, size_t n) noexcept
{
    if (!p || n == 0) return;
    const auto addr = pointer_address(p);
    if (addr > std::numeric_limits<uintptr_t>::max() - (n - 1)) return;
    const size_t page_size = memory_page_size();
    const uintptr_t first = page_base(addr);
    const uintptr_t last = page_base(addr + n - 1);

    try {
        auto& registry = page_lock_registry();
        std::lock_guard lk(registry.mu);
        forget_page_refs(registry, first, last, page_size);
    } catch (const std::system_error& error) {
        platform::safe_println(stderr, "[SecureMem] WARNING: page-lock bookkeeping failed: {}",
                               error.what());
    }
}

template <typename T>
inline size_t locked_page_refcount_for_tests(const T* p) noexcept
{
    try {
        auto& registry = page_lock_registry();
        std::lock_guard lk(registry.mu);
        const auto it = registry.refs.find(page_base(pointer_address(p)));
        return it == registry.refs.end() ? 0 : it->second;
    } catch (...) {
        return 0;
    }
}

inline std::atomic_int& secure_allocation_fail_after() noexcept
{
    static std::atomic_int value{-1};
    return value;
}

inline void inject_secure_allocation_failure(int after_calls) noexcept
{
    secure_allocation_fail_after().store(after_calls);
}

inline void clear_secure_allocation_failure() noexcept
{
    secure_allocation_fail_after().store(-1);
}

inline bool should_fail_secure_allocation() noexcept
{
    int value = secure_allocation_fail_after().load();
    while (value >= 0) {
        const int next = value == 0 ? -1 : value - 1;
        if (secure_allocation_fail_after().compare_exchange_weak(value, next))
            return value == 0;
    }
    return false;
}

} // namespace detail

namespace detail {

inline std::atomic_uint64_t& wiping_deallocation_counter() noexcept
{
    static std::atomic_uint64_t count{0};
    return count;
}

inline std::atomic_bool& all_wipe_observations_zero_flag() noexcept
{
    static std::atomic_bool all_zero{true};
    return all_zero;
}

inline std::atomic_bool& wipe_observation_enabled_flag() noexcept
{
    static std::atomic_bool enabled{false};
    return enabled;
}

inline uint64_t wiping_deallocation_count() noexcept
{
    return wiping_deallocation_counter().load();
}

inline void reset_wipe_observations_for_tests() noexcept
{
    wiping_deallocation_counter().store(0);
    all_wipe_observations_zero_flag().store(true);
    wipe_observation_enabled_flag().store(true);
}

inline bool all_wipe_observations_zero_for_tests() noexcept
{
    wipe_observation_enabled_flag().store(false);
    return all_wipe_observations_zero_flag().load();
}

inline void record_wipe_for_tests(std::span<const std::byte> bytes) noexcept
{
    ++wiping_deallocation_counter();
    if (!wipe_observation_enabled_flag().load()) return;
    if (std::ranges::any_of(bytes, [](std::byte b) { return b != std::byte{0}; }))
        all_wipe_observations_zero_flag().store(false);
}

} // namespace detail

// Allocator for transient plaintext vectors. Every allocation is wiped before
// release, including old capacity released during vector growth or move
// assignment. It deliberately does not mlock: a multi-MiB commit snapshot would
// evict the tree itself from the finite lock budget.
template <typename T>
class WipingAllocator {
public:
    using value_type = T;

    WipingAllocator() noexcept = default;
    template <typename U>
    explicit WipingAllocator(const WipingAllocator<U>&) noexcept
    {}

    [[nodiscard]] T* allocate(size_t n)
    {
        return std::allocator<T>{}.allocate(n);
    }

    void deallocate(T* p, size_t n) const noexcept
    {
        if (p && n > 0) {
            const size_t bytes = n * sizeof(T);
            crypto_wipe(p, bytes);
            detail::record_wipe_for_tests(std::as_bytes(std::span(p, n)));
        }
        std::allocator<T>{}.deallocate(p, n);
    }

    template <typename U>
    friend bool operator==(const WipingAllocator&, const WipingAllocator<U>&) noexcept
    {
        return true;
    }
};

using WipingBytes = std::vector<uint8_t, WipingAllocator<uint8_t>>;

// Vector allocator for variable-length plaintext values that need both sides
// of the secure-memory contract: page-lock while live and wipe before release.
// A header immediately before the payload records whether locking succeeded;
// deallocate must not guess, because an unconditional mem_unlock after a failed
// lock could release a neighboring live secret that shares the same OS page.
template <typename T> class SecureAllocator {
    struct alignas(std::max_align_t) Header {
        size_t bytes;
        bool locked;
    };

    static_assert(alignof(T) <= alignof(std::max_align_t),
                  "SecureAllocator does not support over-aligned values");

public:
    using value_type = T;

    SecureAllocator() noexcept = default;
    template <typename U> explicit SecureAllocator(const SecureAllocator<U>&) noexcept {}

    [[nodiscard]] T* allocate(size_t n)
    {
        if (n > std::numeric_limits<size_t>::max() / sizeof(T)) throw std::bad_array_new_length{};
        const size_t bytes = n * sizeof(T);
        if (bytes > std::numeric_limits<size_t>::max() - sizeof(Header))
            throw std::bad_array_new_length{};
        if (detail::should_fail_secure_allocation()) throw std::bad_alloc{};

        auto* raw = static_cast<std::byte*>(::operator new(sizeof(Header) + bytes));
        auto* header = std::construct_at(static_cast<Header*>(static_cast<void*>(raw)),
                                         Header{.bytes = bytes, .locked = false});
        auto* payload = static_cast<T*>(static_cast<void*>(raw + sizeof(Header)));
        header->locked = detail::mem_lock(payload, bytes);
        if (!header->locked) warn_mlock_failure_once();
        return payload;
    }

    void deallocate(T* p, size_t) const noexcept
    {
        if (!p) return;
        auto* raw = static_cast<std::byte*>(static_cast<void*>(p)) - sizeof(Header);
        auto* header = std::launder(static_cast<Header*>(static_cast<void*>(raw)));
        crypto_wipe(p, header->bytes);
        detail::record_wipe_for_tests(std::as_bytes(std::span(p, header->bytes / sizeof(T))));
        if (header->locked) detail::mem_unlock(p, header->bytes);
        std::destroy_at(header);
        ::operator delete(raw);
    }

    template <typename U>
    friend bool operator==(const SecureAllocator&, const SecureAllocator<U>&) noexcept
    {
        return true;
    }
};

template <typename T> using SecureVector = std::vector<T, SecureAllocator<T>>;

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
        : data_(std::move(other.data_)), size_(other.size_), capacity_(other.capacity_),
          locked_(other.locked_)
    {
        other.size_     = 0;
        other.capacity_ = 0;
        other.locked_   = false;
    }

    SecureBytes& operator=(SecureBytes&& other) noexcept
    {
        if (this != &other) {
            free_storage();
            data_         = std::move(other.data_);
            size_         = other.size_;
            capacity_     = other.capacity_;
            locked_       = other.locked_;
            other.size_   = 0;
            other.capacity_ = 0;
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
            if (detail::should_fail_secure_allocation()) throw std::bad_alloc{};
            data_ = std::make_unique<uint8_t[]>(n);
        } catch (const std::bad_alloc&) {
            platform::safe_println(stderr, "[crypto] SecureBytes alloc of {} bytes failed", n);
            return false;
        }
        size_     = n;
        capacity_ = n;
        locked_   = detail::mem_lock(data_.get(), capacity_);
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

    // Phase 96 (OSV-AUD-003) growth API. The thumbnail pipeline (resized RGB,
    // JPEG encoder callback) appends to a buffer it cannot size up front.
    // Every capacity bump wipes+unlocks+releases the old locked block (observed
    // by the wipe seam), never copying secret bytes into ordinary vectors.

    // Ensure capacity for at least `n` bytes, growing the block and wiping the
    // released block on reallocation. Contents are preserved; false on OOM
    // leaves the object exactly as it was (stb-resize-friendly semantics).
    [[nodiscard]] bool reserve(size_t n)
    {
        if (n <= capacity_) return true;

        size_t new_cap = n;
        constexpr size_t kMinGrow = 16;
        if (capacity_ == 0) {
            new_cap = std::max(n, kMinGrow);
        } else {
            const size_t grown = (capacity_ <= std::numeric_limits<size_t>::max() / 2)
                                     ? capacity_ * 2
                                     : capacity_;
            new_cap = std::max(n, grown);
        }

        try {
            if (detail::should_fail_secure_allocation()) throw std::bad_alloc{};
            auto fresh = std::make_unique<uint8_t[]>(new_cap);
            if (size_) std::memcpy(fresh.get(), data_.get(), size_);
            const size_t old_size = size_;
            free_storage();   // wipes + unlocks + releases the old block
            data_     = std::move(fresh);
            size_     = old_size;
            capacity_ = new_cap;
            locked_   = detail::mem_lock(data_.get(), capacity_);
            if (!locked_) warn_mlock_failure_once();
            return true;
        } catch (const std::bad_alloc&) {
            platform::safe_println(stderr, "[crypto] SecureBytes grow to {} bytes failed",
                                   new_cap);
            return false;
        }
    }

    // Append `src` to the end. False on OOM (contents fully preserved).
    [[nodiscard]] bool append(std::span<const uint8_t> src)
    {
        if (src.empty()) return true;
        if (src.data() == nullptr) return false;
        if (size_ > std::numeric_limits<size_t>::max() - src.size()) return false;
        if (!reserve(size_ + src.size())) return false;
        std::memcpy(data_.get() + size_, src.data(), src.size());
        size_ += src.size();
        return true;
    }

    // Append a single byte. False on OOM (contents fully preserved).
    [[nodiscard]] bool push_back(uint8_t b)
    {
        if (size_ == capacity_ && !reserve(size_ + 1)) return false;
        data_[size_++] = b;
        return true;
    }

    // Wipe contents and release the whole locked block (size_ == capacity_
    // == 0 after). Unlike std::vector::clear the memory AND page lock go too:
    // transient thumbnail/posters should not hold the lock budget.
    void clear() noexcept { free_storage(); }

    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }

    [[nodiscard]] std::span<uint8_t>       span()       noexcept { return {data_.get(), size_}; }
    [[nodiscard]] std::span<const uint8_t> as_span() const noexcept { return {data_.get(), size_}; }

    void wipe() noexcept { if (data_) crypto_wipe(data_.get(), size_); }

private:
    void free_storage() noexcept
    {
        if (!data_) return;
        crypto_wipe(data_.get(), capacity_);
        if (locked_) detail::mem_unlock(data_.get(), capacity_);
        detail::record_wipe_for_tests(std::as_bytes(std::span(data_.get(), capacity_)));
        data_.reset();
        size_     = 0;
        capacity_ = 0;
        locked_   = false;
    }

    std::unique_ptr<uint8_t[]> data_;
    size_t                     size_     = 0;
    size_t                     capacity_ = 0;
    bool                       locked_   = false;
};

} // namespace crypto
