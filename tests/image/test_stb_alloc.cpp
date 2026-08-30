#include "test_framework.h"

#include <cstdint>
#include <cstring>

#include "crypto/secure_mem.h"
#include "image/decode.h"
#include "image/fixtures.h"
#include "image/stb_secure_alloc.h"

// --- Phase 96b (OSV-AUD-003): secure stb_image allocator -------------------
//
// Before 96b the stb_image shim (STBI_MALLOC / STBI_REALLOC_SIZED / STBI_FREE)
// was calloc + free: the full-size decoded RGB buffer was zero-initialised (the
// malformed-JPEG defence) but never page-locked and never wiped on release. The
// shim is now a header-prefixed secure allocator — zero-init, best-effort mlock,
// crypto_wipe before every free/realloc. These tests exercise the allocator
// directly and the full decode path.

namespace {

int read_byte(const void* p, size_t i)
{
    return static_cast<const uint8_t*>(p)[i];
}

bool all_zero(const void* p, size_t n)
{
    const auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i)
        if (b[i] != 0) return false;
    return true;
}

}  // namespace

TEST(stb_secure_alloc_zero_initializes)
{
    void* p = image::stbi_secure_malloc(256);
    REQUIRE(p != nullptr);
    CHECK_TRUE(all_zero(p, 256));
    image::stbi_secure_free(p);
}

TEST(stb_secure_alloc_realloc_zeroes_tail_and_preserves_head)
{
    void* p = image::stbi_secure_malloc(4);
    REQUIRE(p != nullptr);
    auto* bytes = static_cast<uint8_t*>(p);
    bytes[0] = 1; bytes[1] = 2; bytes[2] = 3; bytes[3] = 4;

    void* q = image::stbi_secure_realloc(p, 4, 8);
    REQUIRE(q != nullptr);
    CHECK(read_byte(q, 0) == 1 && read_byte(q, 1) == 2 && read_byte(q, 2) == 3 &&
          read_byte(q, 3) == 4);
    CHECK_TRUE(all_zero(static_cast<const uint8_t*>(q) + 4, 4));   // new tail zeroed
    image::stbi_secure_free(q);
}

TEST(stb_secure_alloc_wipes_on_free)
{
    crypto::detail::reset_wipe_observations_for_tests();
    const uint64_t before = crypto::detail::wiping_deallocation_count();
    void* p = image::stbi_secure_malloc(64);
    REQUIRE(p != nullptr);
    std::memset(p, 0xAB, 64);
    image::stbi_secure_free(p);
    CHECK(crypto::detail::wiping_deallocation_count() > before);
    CHECK_TRUE(crypto::detail::all_wipe_observations_zero_for_tests());
}

TEST(stb_secure_alloc_wipes_released_block_on_realloc)
{
    crypto::detail::reset_wipe_observations_for_tests();
    const uint64_t before = crypto::detail::wiping_deallocation_count();
    void* p = image::stbi_secure_malloc(8);
    REQUIRE(p != nullptr);
    std::memset(p, 0x5A, 8);
    void* q = image::stbi_secure_realloc(p, 8, 64);
    REQUIRE(q != nullptr);
    // The released 8-byte block was wiped, not just freed.
    CHECK(crypto::detail::wiping_deallocation_count() > before);
    CHECK_TRUE(crypto::detail::all_wipe_observations_zero_for_tests());
    image::stbi_secure_free(q);
}

TEST(stb_secure_alloc_locks_payload_best_effort)
{
    void* p = image::stbi_secure_malloc(8192);
    REQUIRE(p != nullptr);
    const auto* ptr = static_cast<const uint8_t*>(p);
    const size_t after_alloc = crypto::detail::locked_page_refcount_for_tests(ptr);
    CHECK(after_alloc >= 1);                       // payload page is held in the lock registry
    image::stbi_secure_free(p);
    const size_t after_free = crypto::detail::locked_page_refcount_for_tests(ptr);
    CHECK(after_free < after_alloc);               // our ref(s) were released
}

TEST(stb_secure_alloc_realloc_failure_keeps_old_block)
{
    void* p = image::stbi_secure_malloc(4);
    REQUIRE(p != nullptr);
    auto* bytes = static_cast<uint8_t*>(p);
    bytes[0] = 0x0A; bytes[1] = 0x0B; bytes[2] = 0x0C; bytes[3] = 0x0D;

    crypto::detail::inject_secure_allocation_failure(0);
    void* q = image::stbi_secure_realloc(p, 4, 8);
    crypto::detail::clear_secure_allocation_failure();

    CHECK(q == nullptr);                          // allocation failed
    // stb contract: the old block survives intact and stays locked.
    CHECK(read_byte(p, 0) == 0x0A && read_byte(p, 1) == 0x0B &&
          read_byte(p, 2) == 0x0C && read_byte(p, 3) == 0x0D);
    CHECK(crypto::detail::locked_page_refcount_for_tests(static_cast<const uint8_t*>(p)) >= 1);
    image::stbi_secure_free(p);
}

TEST(decode_stb_wipes_intermediate_raw_buffer)
{
    // OSV-AUD-003 point #1: stb's full-size RGB decode landed in an ordinary
    // calloc block, was copied into SecureBytes pixels, then freed WITHOUT a
    // wipe. With the secure allocator the intermediate is wiped on
    // stbi_image_free — observed via the wipe seam.
    crypto::detail::reset_wipe_observations_for_tests();
    const uint64_t before = crypto::detail::wiping_deallocation_count();
    const auto img = image::decode_from_memory(fixtures::solid_jpeg(16, 8, 200, 100, 50));
    REQUIRE(img.has_value());
    CHECK_EQ(img->width, 16);
    CHECK_EQ(img->height, 8);
    CHECK(crypto::detail::wiping_deallocation_count() > before);
    CHECK_TRUE(crypto::detail::all_wipe_observations_zero_for_tests());
}