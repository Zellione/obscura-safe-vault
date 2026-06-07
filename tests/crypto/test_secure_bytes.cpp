#include "test_framework.h"

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
