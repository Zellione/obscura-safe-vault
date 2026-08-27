#include "test_framework.h"

#include <array>
#include <atomic>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_set>

#include "crypto/secure_string.h"

using namespace std::string_view_literals;

namespace {

std::atomic_int wipe_observations{0};
std::atomic_bool wipe_observed_nonzero{false};

void observe_wiped_allocation(std::span<const uint8_t> bytes) noexcept
{
    ++wipe_observations;
    for (const uint8_t b : bytes)
        if (b != 0) wipe_observed_nonzero.store(true);
}

}  // namespace

// SecureString (Phase 91) is the secure string type for the index tree's
// human-readable metadata: an mlock'd (best-effort), crypto_wipe'd-on-destroy,
// copyable string with no SSO, so even short names never linger in freed heap.

TEST(secure_string_default_is_empty)
{
    crypto::SecureString s;
    CHECK_TRUE(s.empty());
    CHECK_EQ(s.size(), static_cast<size_t>(0));
    CHECK_TRUE(s.view().empty());
}

TEST(secure_string_constructs_from_string_view)
{
    crypto::SecureString s(std::string_view{"caf\xC3\xA9"
                                            ".jpg"});
    CHECK_EQ(s.size(), static_cast<size_t>(9));
    CHECK_EQ(s.view(), "caf\xC3\xA9"
                       ".jpg"sv);
}

TEST(secure_string_constructs_from_literal_and_std_string)
{
    crypto::SecureString a("photo.jpg");
    CHECK_EQ(a.view(), "photo.jpg"sv);

    const std::string plain = "subgallery";
    crypto::SecureString b(plain);
    CHECK_EQ(b.view(), "subgallery"sv);
}

TEST(secure_string_assign_replaces_contents)
{
    crypto::SecureString s("first");
    REQUIRE(s.size() == 5);
    REQUIRE(s.assign("second"));
    CHECK_EQ(s.size(), static_cast<size_t>(6));
    CHECK_EQ(s.view(), "second"sv);
    CHECK_FALSE(s.assign(std::string_view{nullptr, 5}));  // null+non-zero is rejected
    CHECK_EQ(s.view(), "second"sv);                       // rejection leaves contents intact
}

TEST(secure_string_assign_from_own_view_is_safe)
{
    crypto::SecureString s("abcdef");
    REQUIRE(s.assign(s.view().substr(1, 4)));
    CHECK_EQ(s.view(), "bcde"sv);
}

TEST(secure_string_failed_assign_preserves_original)
{
    crypto::SecureString s("keep-me");
    crypto::detail::inject_secure_allocation_failure(0);
    CHECK_FALSE(s.assign("replacement"));
    crypto::detail::clear_secure_allocation_failure();
    CHECK_EQ(s.view(), "keep-me"sv);
}

TEST(secure_string_assignment_operator_covers_string_like_inputs)
{
    crypto::SecureString s;
    s = "literal";
    CHECK_EQ(s.view(), "literal"sv);
    s = std::string{"from std string"};
    CHECK_EQ(s.view(), "from std string"sv);
    s = std::string_view{"from view"};
    CHECK_EQ(s.view(), "from view"sv);
}

TEST(secure_string_copy_is_a_deep_independent_buffer)
{
    crypto::SecureString a("name.jpg");
    crypto::SecureString b(a);
    CHECK_EQ(b.view(), "name.jpg"sv);
    // Mutating the source must not affect the copy.
    a = "changed";
    CHECK_EQ(a.view(), "changed"sv);
    CHECK_EQ(b.view(), "name.jpg"sv);
}

TEST(secure_string_copy_assignment_swaps_contents)
{
    crypto::SecureString a("aaaa");
    crypto::SecureString b("bbbb");
    a = b;
    CHECK_EQ(a.view(), "bbbb"sv);
    CHECK_EQ(b.view(), "bbbb"sv);
    b = "cccc";
    CHECK_EQ(b.view(), "cccc"sv);
    CHECK_EQ(a.view(), "bbbb"sv);
}

TEST(secure_string_move_transfers_ownership)
{
    crypto::SecureString a("moved");
    const char* bytes = a.view().data();
    crypto::SecureString b(std::move(a));
    CHECK_EQ(b.view(), "moved"sv);
    CHECK(b.view().data() == bytes);  // moved, buffer not copied
    CHECK_TRUE(a.empty());
}

TEST(secure_string_move_assignment_frees_previous)
{
    crypto::SecureString a("keep-me");
    crypto::SecureString b("old");
    a = std::move(b);
    CHECK_EQ(a.view(), "old"sv);
    CHECK_TRUE(b.empty());
}

TEST(secure_string_resize_zeroes_and_is_writable)
{
    crypto::SecureString s;
    REQUIRE(s.resize(6));
    CHECK_EQ(s.size(), static_cast<size_t>(6));
    // The deserialisation pattern: fill the buffer in place after resize.
    const uint8_t bytes[] = {'a', 'b', 'c', 'd', 'e', 'f'};
    std::memcpy(s.data(), bytes, 6);
    CHECK_EQ(s.view(), "abcdef"sv);
    REQUIRE(s.resize(0));
    CHECK_TRUE(s.empty());
}

TEST(secure_string_view_is_non_owning_read_access)
{
    crypto::SecureString s("view-only");
    const std::string_view v = s.view();
    CHECK_EQ(v, "view-only"sv);
}

TEST(secure_string_equality_against_every_string_kind)
{
    crypto::SecureString s("tag:a");
    CHECK(s == crypto::SecureString("tag:a"));
    CHECK(s == std::string_view("tag:a"));
    CHECK(s == std::string("tag:a"));
    CHECK(s == "tag:a");
    CHECK("tag:a" == s);
    CHECK_FALSE(s == "tag:b");
    CHECK(s != "tag:b");
}

TEST(secure_string_ordering_is_lexicographic)
{
    crypto::SecureString a("alpha");
    crypto::SecureString b("beta");
    CHECK(a < b);
    CHECK(a < std::string_view{"beta"});
    CHECK(std::string_view{"alpha"} < b);
    CHECK(b > a);
    CHECK(a <= "alpha");
}

TEST(secure_string_is_hashable)
{
    crypto::SecureString a("hashme");
    crypto::SecureString b("hashme");
    crypto::SecureString c("other");
    std::unordered_set<crypto::SecureString> set;
    set.insert(std::move(a));
    CHECK_TRUE(set.contains(b));
    CHECK_FALSE(set.contains(c));
}

TEST(secure_string_wipe_zeroes_storage)
{
    crypto::SecureString s("secret-name");
    REQUIRE(!s.empty());
    CHECK_EQ(s.view(), "secret-name"sv);
    s.wipe();
    // wipe zeroes the bytes; the length is untouched (destruction wipes again).
    bool all_zero = true;
    for (size_t i = 0; i < s.size(); ++i)
        if (s.data()[i] != 0) all_zero = false;
    CHECK_TRUE(all_zero);
}

TEST(secure_string_clear_empties)
{
    crypto::SecureString s("clear-me");
    s.clear();
    CHECK_TRUE(s.empty());
    CHECK_EQ(s.size(), static_cast<size_t>(0));
}

TEST(secure_string_is_mlocked)
{
    crypto::SecureString s(static_cast<std::string_view>("a-modestly-sized-node-name.jpg"));
    // The same budget that makes SecureBytes lockable is in force here, so a
    // fresh allocation is page-locked on any normal host (Phase 89's
    // decoded_pixels_are_mlocked pins the identical invariant for pixels).
    CHECK_TRUE(s.is_locked());
}

TEST(secure_mem_overlapping_page_locks_are_reference_counted)
{
    // Normal allocator blocks frequently share a page. Releasing one range
    // must not munlock a page still occupied by another secure allocation.
    alignas(65536) std::array<uint8_t, 65536> page{};
    REQUIRE(crypto::detail::memory_page_size() <= page.size());
    REQUIRE(crypto::detail::mem_lock(page.data(), 1));
    REQUIRE(crypto::detail::mem_lock(page.data() + 1, 1));
    CHECK_EQ(crypto::detail::locked_page_refcount_for_tests(page.data()),
             static_cast<size_t>(2));
    crypto::detail::mem_unlock(page.data(), 1);
    CHECK_EQ(crypto::detail::locked_page_refcount_for_tests(page.data()),
             static_cast<size_t>(1));
    crypto::detail::mem_unlock(page.data() + 1, 1);
    CHECK_EQ(crypto::detail::locked_page_refcount_for_tests(page.data()),
             static_cast<size_t>(0));
}

TEST(wiping_bytes_zeroes_every_allocation_before_release)
{
    wipe_observations.store(0);
    wipe_observed_nonzero.store(false);
    crypto::detail::set_wipe_observer_for_tests(&observe_wiped_allocation);
    {
        crypto::WipingBytes bytes(64, 0xA5);
        bytes.reserve(1024);  // exercises a reallocation, not only destruction
        bytes.assign(80, 0x5A);
    }
    crypto::detail::set_wipe_observer_for_tests(nullptr);
    CHECK(wipe_observations.load() >= 2);
    CHECK_FALSE(wipe_observed_nonzero.load());
}
