#pragma once

// Secure, zero-initialised allocator backing stb_image's STBI_MALLOC /
// STBI_REALLOC_SIZED / STBI_FREE (Phase 96b, OSV-AUD-003).
//
// Before 96b the shim in src/image/decode.cpp was calloc + free: the full-size
// decoded RGB buffer was zero-initialised (the malformed-JPEG defence — stb's
// colour conversion reads plane regions a failed decode never wrote) but never
// page-locked and never wiped on release. This allocator keeps the zero-init
// and adds best-effort page lock + crypto_wipe before every free/reallocation,
// so OSV-AUD-003 point #1 (the full-size stb decode landing in ordinary, unwiped
// storage) is closed.
//
// Layout: a fixed header is stored IMMEDIATELY BEFORE the payload, because
// STBI_FREE(p) — stb's C contract — carries no size; the length is recovered
// from the header instead of a global map (thread-safe: decode runs on both the
// main and the DecodeWorker thread). Only the payload is locked and wiped; the
// header is non-secret bookkeeping. Realloc keeps stb's semantics: on failure
// it returns nullptr and the OLD block stays alive (stb's error paths still
// free it). Allocation failures honour crypto::detail::should_fail_secure_allocation
// so tests can inject them deterministically.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>

#include "crypto/secure_mem.h"

namespace image {

namespace detail {

struct StbAllocHeader {
    size_t size;    // payload bytes
    bool   locked;  // best-effort page-lock succeeded for the payload
};

inline constexpr size_t kStbAllocHeaderSize = sizeof(StbAllocHeader);

}  // namespace detail

// Allocate `sz` payload bytes, zero-initialised, best-effort page-locked, with
// the length carried in a header so the payload can be wiped on release. A zero
// request still gets a unique 1-byte payload (matches the old calloc(1, 0)
// behaviour of returning a non-null reply).
inline void* stbi_secure_malloc(size_t sz) // NOSONAR cpp:S5008 — mandated by stb's C allocator macro contract
{
    if (crypto::detail::should_fail_secure_allocation()) return nullptr;

    // Validate length arithmetic: header + payload must not overflow.
    if (sz > std::numeric_limits<size_t>::max() - detail::kStbAllocHeaderSize - 1)
        return nullptr;
    const size_t payload = sz == 0 ? 1 : sz;
    const size_t total   = detail::kStbAllocHeaderSize + payload;

    auto* block = static_cast<detail::StbAllocHeader*>(std::calloc(1, total)); // NOSONAR cpp:S1231 — stb frees via STBI_FREE, so the shim must stay malloc-family
    if (!block) return nullptr;
    // NOSONAR cpp:S6022 — a C-allocator shim: the payload must be handed to stb
    // as void* and to the codebase's uint8_t secure-memory APIs (mem_lock /
    // crypto_wipe), so uint8_t is the canonical byte type here, not std::byte.
    auto* data    = reinterpret_cast<uint8_t*>(block + 1); // NOSONAR cpp:S6022
    block->size   = sz;
    block->locked = crypto::detail::mem_lock(data, payload);
    if (!block->locked) crypto::warn_mlock_failure_once();
    return data;
}

inline void stbi_secure_free(void* p) noexcept // NOSONAR cpp:S5008
{
    if (!p) return;
    auto* block = static_cast<detail::StbAllocHeader*>(p) - 1;
    auto* data  = static_cast<uint8_t*>(p);
    const size_t payload = block->size == 0 ? 1 : block->size;
    if (block->size) {
        crypto_wipe(data, block->size);
        crypto::detail::record_wipe_for_tests(std::as_bytes(std::span(data, block->size)));
    }
    if (block->locked) crypto::detail::mem_unlock(data, payload);
    std::free(block); // NOSONAR cpp:S1231 — releasing a block this shim allocated
}

inline void* stbi_secure_realloc(void* p, size_t oldsz, size_t newsz) // NOSONAR cpp:S5008 cpp:S954
{
    if (!p) return stbi_secure_malloc(newsz);
    void* q = stbi_secure_malloc(newsz);
    if (!q) return nullptr;   // stb contract: old block stays alive on failure
    std::memcpy(q, p, oldsz < newsz ? oldsz : newsz);   // new tail stays zero
    stbi_secure_free(p);
    return q;
}

} // namespace image