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

// --- Phase 96 (OSV-AUD-003): growable SecureBytes -------------------------
//
// make_thumbnail() outputs resized RGB pixels and JPEG bytes into a buffer
// that grows one chunk at a time (the stbi_write callback appends per row).
// Before Phase 96 those sinks were std::vector<uint8_t> — never wiped, never
// page-locked. The growth API below extends SecureBytes so thumbnail/staging/
// transfer buffers get the same mlock + wipe guarantees as every other secret.

TEST(secure_bytes_append_copies_and_extends)
{
    const std::array<uint8_t, 4> a{{1, 2, 3, 4}};
    const std::array<uint8_t, 3> b{{5, 6, 7}};
    crypto::SecureBytes buf;
    REQUIRE(buf.append(a));
    REQUIRE(buf.append(b));
    CHECK_EQ(buf.size(), static_cast<size_t>(7));
    const std::array<uint8_t, 7> expected{{1, 2, 3, 4, 5, 6, 7}};
    CHECK_BYTES_EQ(buf.as_span(), std::span(expected));
}

TEST(secure_bytes_push_back_appends_byte)
{
    crypto::SecureBytes buf;
    REQUIRE(buf.push_back(0xAB));
    REQUIRE(buf.push_back(0xCD));
    CHECK_EQ(buf.size(), static_cast<size_t>(2));
    CHECK_EQ(buf[0], uint8_t{0xAB});
    CHECK_EQ(buf[1], uint8_t{0xCD});
}

TEST(secure_bytes_reserve_grows_capacity_without_realloc_on_append)
{
    crypto::SecureBytes buf;
    REQUIRE(buf.reserve(256));
    CHECK(buf.capacity() >= 256);
    const uint8_t* p = buf.data();
    REQUIRE(buf.append(std::array<uint8_t, 64>{1}));
    CHECK(buf.data() == p);                             // no reallocation happened
    CHECK_EQ(buf.size(), static_cast<size_t>(64));
}

TEST(secure_bytes_reserve_zero_is_noop)
{
    crypto::SecureBytes buf;
    CHECK_TRUE(buf.reserve(0));
    CHECK_TRUE(buf.reserve(8));
    CHECK(buf.capacity() >= 8);
}

TEST(secure_bytes_growth_realloc_wipes_released_capacity)
{
    // Every capacity bump releases the old locked block; each release must be
    // wiped (OSV-AUD-003: wipe-before-reallocation). Observed via the seam.
    crypto::detail::reset_wipe_observations_for_tests();
    const uint64_t before = crypto::detail::wiping_deallocation_count();
    crypto::SecureBytes buf;
    for (int i = 0; i < 2000; ++i)
        REQUIRE(buf.push_back(static_cast<uint8_t>(i % 251)));
    CHECK(crypto::detail::wiping_deallocation_count() > before);
    CHECK_EQ(buf.size(), static_cast<size_t>(2000));
    // Every released block was actually zeroed (record_wipe observes 0s).
    CHECK_TRUE(crypto::detail::all_wipe_observations_zero_for_tests());
}

TEST(secure_bytes_growth_alloc_failure_preserves_contents)
{
    crypto::SecureBytes buf;
    const std::array<uint8_t, 4> seed{{1, 2, 3, 4}};
    REQUIRE(buf.append(seed));
    crypto::detail::inject_secure_allocation_failure(0);
    const std::array<uint8_t, 16> more{{9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9}};
    CHECK_FALSE(buf.append(more));
    crypto::detail::clear_secure_allocation_failure();
    CHECK_EQ(buf.size(), static_cast<size_t>(4));   // contents fully intact
    CHECK_BYTES_EQ(buf.as_span(), std::span(seed));
}

TEST(secure_bytes_clear_wipes_and_releases)
{
    crypto::detail::reset_wipe_observations_for_tests();
    crypto::SecureBytes buf;
    const std::array<uint8_t, 8> seed{{0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A}};
    REQUIRE(buf.append(seed));
    buf.clear();
    CHECK_TRUE(buf.empty());
    CHECK_EQ(buf.size(), static_cast<size_t>(0));
    CHECK_EQ(buf.capacity(), static_cast<size_t>(0));   // lock budget released
    CHECK_TRUE(crypto::detail::all_wipe_observations_zero_for_tests());
}
