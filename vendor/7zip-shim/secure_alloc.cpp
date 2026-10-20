// Private to libosv7zip: linker wrapping changes SDK allocations only, never
// the application's allocator. Each mapping owns its pages, so unlocking one
// buffer cannot unlock another. All releases wipe, including realloc.
#include "monocypher.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#endif

namespace {
constexpr size_t max_live_bytes = size_t{1} << 30;
std::atomic_size_t live_bytes{0};
std::atomic_bool degraded{false};

struct alignas(std::max_align_t) Header {
    void* mapping;
    size_t mapping_size;
    size_t payload_size;
    bool locked;
};

void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) noexcept
{
    if (size > max_live_bytes || alignment > max_live_bytes || alignment < sizeof(void*) ||
        (alignment & (alignment - 1)) != 0)
        return nullptr;
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return nullptr;
    const auto page = static_cast<size_t>(page_size);
    const size_t needed = std::max(size, size_t{1}) + sizeof(Header) + alignment - 1;
    const size_t mapped = (needed + page - 1) / page * page;
    if (mapped > max_live_bytes) return nullptr;
    size_t current = live_bytes.load();
    do {
        if (current > max_live_bytes - mapped) return nullptr;
    } while (!live_bytes.compare_exchange_weak(current, current + mapped));
    void* base = mmap(nullptr, mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        live_bytes.fetch_sub(mapped);
        return nullptr;
    }
    const bool locked = mlock(base, mapped) == 0;
    if (!locked) degraded.store(true);
    (void)madvise(base, mapped, MADV_DONTDUMP);
    const auto start = reinterpret_cast<uintptr_t>(base) + sizeof(Header);
    auto* result = reinterpret_cast<void*>((start + alignment - 1) & ~(alignment - 1));
    auto* header = static_cast<Header*>(result) - 1;
    *header = {base, mapped, size, locked};
#if defined(__SANITIZE_ADDRESS__)
    // mmap bypasses ASAN's malloc interceptor: explicitly poison tail padding.
    const size_t offset =
        static_cast<size_t>(static_cast<char*>(result) - static_cast<char*>(base));
    __asan_poison_memory_region(static_cast<char*>(result) + size, mapped - offset - size);
#endif
    return result;
}

void release(void* ptr) noexcept
{
    if (!ptr) return;
    const Header header = *(static_cast<Header*>(ptr) - 1);
#if defined(__SANITIZE_ADDRESS__)
    __asan_unpoison_memory_region(header.mapping, header.mapping_size);
#endif
    crypto_wipe(header.mapping, header.mapping_size);
    if (header.locked) (void)munlock(header.mapping, header.mapping_size);
    (void)munmap(header.mapping, header.mapping_size);
    live_bytes.fetch_sub(header.mapping_size);
}

void* throwing_allocate(size_t size)
{
    if (void* ptr = allocate(size)) return ptr;
    throw std::bad_alloc();
}
}  // namespace

extern "C" {
int osv7zip_memory_degraded()
{
    return degraded.load() ? 1 : 0;
}
size_t osv7zip_live_bytes()
{
    return live_bytes.load();
}
void* __wrap_malloc(size_t size)
{
    return allocate(size);
}
void __wrap_free(void* ptr)
{
    release(ptr);
}
void* __wrap_calloc(size_t count, size_t size)
{
    if (size != 0 && count > std::numeric_limits<size_t>::max() / size) return nullptr;
    return allocate(count * size);  // Anonymous mappings start zeroed.
}
void* __wrap_realloc(void* ptr, size_t size)
{
    if (!ptr) return allocate(size);
    if (size == 0) {
        release(ptr);
        return nullptr;
    }
    void* replacement = allocate(size);
    if (!replacement) return nullptr;  // Preserve the old allocation on failure.
    const auto* header = static_cast<Header*>(ptr) - 1;
    std::memcpy(replacement, ptr, std::min(size, header->payload_size));
    release(ptr);
    return replacement;
}
int __wrap_posix_memalign(void** ptr, size_t alignment, size_t size)
{
    if (alignment < sizeof(void*) || (alignment & (alignment - 1)) != 0) return EINVAL;
    void* result = allocate(size, alignment);
    if (!result) return ENOMEM;
    *ptr = result;
    return 0;
}
void* __wrap__Znwm(size_t size)
{
    return throwing_allocate(size);
}
void* __wrap__Znam(size_t size)
{
    return throwing_allocate(size);
}
void __wrap__ZdlPv(void* ptr)
{
    release(ptr);
}
void __wrap__ZdaPv(void* ptr)
{
    release(ptr);
}
void __wrap__ZdlPvm(void* ptr, size_t)
{
    release(ptr);
}
void __wrap__ZdaPvm(void* ptr, size_t)
{
    release(ptr);
}
}
