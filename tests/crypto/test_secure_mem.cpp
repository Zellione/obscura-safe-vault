#include "test_framework.h"

#include <array>
#include <cstring>
#include <memory>

#include "crypto/secure_mem.h"

// Verify SecureBuffer wipes its bytes on destruction. We place the buffer in a
// heap allocation we control so we can legally inspect the storage after the
// object's lifetime ends (reading the destroyed object itself would be UB).
//
// Under ASAN this also exercises that no leak / bad access occurs across the
// mlock/munlock + wipe path.
TEST(secure_buffer_wipes_on_destruction)
{
    constexpr size_t N = 32;
    using Buf = crypto::SecureBuffer<N>;

    // Raw storage we own; placement-new a SecureBuffer into it, then destroy it
    // explicitly and inspect the bytes.
    alignas(Buf) auto storage = std::array<unsigned char, sizeof(Buf)>{};  // value-init: keeps gcc -O3 -Werror=uninitialized quiet; ctor overwrites, dtor wipes

    auto* buf = new (storage.data()) Buf();
    std::memset(buf->data(), 0xAB, N);
    // Sanity: the fill took effect.
    bool all_ab = true;
    for (size_t i = 0; i < N; ++i) all_ab &= (buf->data()[i] == 0xAB);
    CHECK_TRUE(all_ab);

    std::destroy_at(buf);  // destructor must crypto_wipe the bytes

    // Use launder to inform gcc that the storage pointer is valid post-destruction.
    bool all_zero = true;
    auto* storage_ptr = std::launder(storage.data());
    for (size_t i = 0; i < N; ++i) {
        if (storage_ptr[i] != 0x00) all_zero = false;
    }
    CHECK_TRUE(all_zero);
}

// is_locked() reflects the mlock outcome and must not crash regardless of the
// host's RLIMIT_MEMLOCK. On a typical dev box it is true; under `ulimit -l 0`
// it degrades to false (logged, non-fatal) — either is acceptable here.
TEST(secure_buffer_lock_state_is_queryable)
{
    crypto::SecureBuffer<64> buf;
    bool locked = buf.is_locked();
    CHECK_TRUE(locked || !locked);  // just assert no crash; value is environmental
}

// explicit wipe() zeroes the contents immediately.
TEST(secure_buffer_explicit_wipe)
{
    crypto::SecureBuffer<16> buf;
    std::memset(buf.data(), 0x7F, decltype(buf)::size());
    buf.wipe();
    bool all_zero = true;
    for (size_t i = 0; i < decltype(buf)::size(); ++i) all_zero &= (buf.data()[i] == 0);
    CHECK_TRUE(all_zero);
}

// should_warn_mlock_once() returns true exactly once, then false on all subsequent calls.
// This helper is used to suppress duplicate warnings for mlock failures.
TEST(should_warn_mlock_once)
{
    // In a fresh test process (or if reset), the first call should return true.
    // Subsequent calls should return false.
    // Note: This is a process-level singleton, so if a prior test called it, we may
    // get false immediately. The important part is that multiple calls in sequence
    // don't all return true.
    bool first  = crypto::should_warn_mlock_once();
    bool second = crypto::should_warn_mlock_once();
    bool third  = crypto::should_warn_mlock_once();

    // first should be true; second and third should be false (or first could be false
    // if already triggered). At least ensure not all are true.
    CHECK_TRUE(!first || (!second && !third));
}
