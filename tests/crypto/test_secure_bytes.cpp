#include "test_framework.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "crypto/secure_mem.h"

// SecureBytes is the runtime-sized sibling of SecureBuffer<N>: an mlock'd,
// auto-wiped heap buffer for secrets whose length isn't known at compile time
// (decrypted image bytes returned by Vault::read_image — invariant #1).

TEST(secure_bytes_allocates_requested_size)
{
    crypto::SecureBytes buf(1024);
    CHECK_EQ(buf.size(), static_cast<size_t>(1024));
    CHECK_FALSE(buf.empty());
    CHECK(buf.data() != nullptr);
}

TEST(secure_bytes_default_is_empty)
{
    crypto::SecureBytes buf;
    CHECK_TRUE(buf.empty());
    CHECK_EQ(buf.size(), static_cast<size_t>(0));
}

TEST(secure_bytes_is_writable_and_readable)
{
    crypto::SecureBytes buf(256);
    for (size_t i = 0; i < buf.size(); ++i) buf.data()[i] = static_cast<uint8_t>(i);
    bool ok = true;
    for (size_t i = 0; i < buf.size(); ++i)
        if (buf.data()[i] != static_cast<uint8_t>(i)) ok = false;
    CHECK_TRUE(ok);
}

TEST(secure_bytes_resize_changes_size)
{
    crypto::SecureBytes buf;
    REQUIRE(buf.resize(64));
    CHECK_EQ(buf.size(), static_cast<size_t>(64));
    REQUIRE(buf.resize(0));
    CHECK_TRUE(buf.empty());
}

TEST(secure_bytes_wipe_zeroes_storage)
{
    crypto::SecureBytes buf(32);
    for (size_t i = 0; i < buf.size(); ++i) buf.data()[i] = 0xAB;
    buf.wipe();
    bool all_zero = true;
    for (size_t i = 0; i < buf.size(); ++i)
        if (buf.data()[i] != 0) all_zero = false;
    CHECK_TRUE(all_zero);
}

TEST(secure_bytes_move_transfers_ownership)
{
    crypto::SecureBytes a(48);
    for (size_t i = 0; i < a.size(); ++i) a.data()[i] = static_cast<uint8_t>(i + 1);
    uint8_t* original = a.data();

    crypto::SecureBytes b(std::move(a));
    CHECK_EQ(b.size(), static_cast<size_t>(48));
    CHECK(b.data() == original);  // moved, not copied
    CHECK_TRUE(a.empty());        // source emptied
    CHECK_EQ(b.data()[0], static_cast<uint8_t>(1));
}

TEST(secure_bytes_span_view_matches_data)
{
    crypto::SecureBytes buf(16);
    for (size_t i = 0; i < buf.size(); ++i) buf.data()[i] = static_cast<uint8_t>(i * 3);
    auto sp = buf.as_span();
    REQUIRE(sp.size() == buf.size());
    CHECK(sp.data() == buf.data());
}

TEST(secure_bytes_operator_index_read_write)
{
    crypto::SecureBytes buf(8);
    for (size_t i = 0; i < buf.size(); ++i) buf[i] = static_cast<uint8_t>(i * 7);
    bool ok = true;
    for (size_t i = 0; i < buf.size(); ++i)
        if (buf[i] != static_cast<uint8_t>(i * 7)) ok = false;
    CHECK_TRUE(ok);
}

TEST(secure_bytes_assign_copies_source)
{
    const uint8_t src[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
    crypto::SecureBytes buf;
    REQUIRE(buf.assign(std::span(src)));
    CHECK_EQ(buf.size(), sizeof(src));
    bool ok = true;
    for (size_t i = 0; i < sizeof(src); ++i)
        if (buf[i] != src[i]) ok = false;
    CHECK_TRUE(ok);
}

TEST(secure_bytes_assign_replaces_previous_contents)
{
    const uint8_t first[] = {1, 2, 3, 4, 5, 6};
    const uint8_t second[] = {9};
    crypto::SecureBytes buf;
    REQUIRE(buf.assign(std::span(first)));
    REQUIRE(buf.assign(std::span(second)));
    CHECK_EQ(buf.size(), static_cast<size_t>(1));
    CHECK_EQ(buf[0], 9);
}

// Phase 6c: the UI needs to know whether ANY mlock has failed this process
// (decoded data then sits in swappable memory). Order-independent: whatever
// earlier tests did to the process-wide flag, seen() becomes true after the
// first warn-gate call and stays true, while the gate itself stays exhausted.
TEST(mlock_failure_seen_tracks_the_warn_gate)
{
    (void)crypto::should_warn_mlock_once();
    CHECK_TRUE(crypto::mlock_failure_seen());
    CHECK_FALSE(crypto::should_warn_mlock_once());
    CHECK_TRUE(crypto::mlock_failure_seen());
}

TEST(secure_bytes_destruction_records_wipe_observation)
{
    // OSV-AUD-006: read_keyfile/unlock_job secrets are SecureBytes; the wipe
    // observation seam must therefore cover SecureBytes deallocation so a
    // partial-read/launch-failure test can prove bytes already read are wiped.
    crypto::detail::reset_wipe_observations_for_tests();
    {
        crypto::SecureBytes buf(64);
        std::fill_n(buf.data(), buf.size(), 0xA5);
    }
    CHECK(crypto::detail::wiping_deallocation_count() >= 1);
    CHECK_TRUE(crypto::detail::all_wipe_observations_zero_for_tests());
}

TEST(secure_bytes_injected_allocation_failure_returns_false)
{
    crypto::SecureBytes buf;
    crypto::detail::inject_secure_allocation_failure(0);
    CHECK_FALSE(buf.resize(32));
    crypto::detail::clear_secure_allocation_failure();
    CHECK_TRUE(buf.empty());
}
