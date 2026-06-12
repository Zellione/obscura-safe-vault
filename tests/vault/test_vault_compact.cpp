#include "test_framework.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "image/fixtures.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

// Phase 7: compaction. Vault::compact() rewrites the vault with only live
// chunks (deleted images' chunks and superseded index blobs are dropped),
// atomically replacing the original file. wasted_bytes() reports how much of
// the data region is reclaimable.

static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_ctest_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

static uint64_t size_on_disk(const fs::path& p)
{
    std::error_code ec;
    const auto s = fs::file_size(p, ec);
    return ec ? 0 : static_cast<uint64_t>(s);
}

TEST(wasted_bytes_tracks_orphaned_chunks)
{
    TempVault tv("waste");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    CHECK_EQ(v.wasted_bytes(), 0u);  // fresh vault: header + live index only

    const size_t img_size = 100 * 1024;
    REQUIRE(v.add_image("", pattern(img_size, 1), "a.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(img_size, 2), "b.bin") == vault::VaultResult::Ok);

    const uint64_t before = v.wasted_bytes();  // superseded index blobs only
    REQUIRE(v.remove_image("", "a.bin") == vault::VaultResult::Ok);
    // The orphaned chunk (incompressible 100 KiB + AEAD framing) is now waste.
    CHECK_TRUE(v.wasted_bytes() >= before + img_size);
}

TEST(compact_reclaims_space_and_preserves_remaining_images)
{
    TempVault tv("reclaim");
    const auto keep = pattern(80 * 1024, 7);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    // Dead chunks total ~200 KiB — below the auto-compact threshold, so the
    // waste is still there for the explicit compact() below to reclaim.
    REQUIRE(v.add_image("", pattern(100 * 1024, 3), "gone1.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", keep, "keep.bin")                    == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(100 * 1024, 4), "gone2.bin") == vault::VaultResult::Ok);
    REQUIRE(v.remove_image("", "gone1.bin") == vault::VaultResult::Ok);
    REQUIRE(v.remove_image("", "gone2.bin") == vault::VaultResult::Ok);

    const uint64_t size_before = size_on_disk(tv.path);
    REQUIRE(v.compact() == vault::VaultResult::Ok);
    const uint64_t size_after = size_on_disk(tv.path);

    CHECK_TRUE(size_after + 200 * 1024 <= size_before);  // both dead chunks gone
    CHECK_EQ(v.wasted_bytes(), 0u);

    // Still usable in-session after the rewrite...
    auto kids = v.list("");
    REQUIRE(kids.size() == 1);
    crypto::SecureBytes out;
    REQUIRE(v.read_image(*kids[0], out) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(keep));

    // ...and the vault can still grow afterwards.
    REQUIRE(v.add_image("", pattern(1000, 5), "new.bin") == vault::VaultResult::Ok);
    CHECK_EQ(v.list("").size(), 2u);
}

TEST(compact_preserves_structure_thumbnails_and_survives_reopen)
{
    TempVault tv("structure");
    const auto png = fixtures::solid_png(64, 48, 10, 200, 30);  // gets a thumbnail

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("trips/2026")                    == vault::VaultResult::Ok);
        REQUIRE(v.add_image("trips/2026", png, "pic.png")         == vault::VaultResult::Ok);
        REQUIRE(v.add_image("trips/2026", pattern(5000, 8), "raw.bin")
                == vault::VaultResult::Ok);
        REQUIRE(v.remove_image("trips/2026", "raw.bin") == vault::VaultResult::Ok);
        REQUIRE(v.compact() == vault::VaultResult::Ok);
    }

    // A compacted vault must reopen and unlock from a cold start.
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    REQUIRE(v2.list("").size() == 1);
    auto kids = v2.list("trips/2026");
    REQUIRE(kids.size() == 1);
    CHECK_EQ(kids[0]->name, std::string("pic.png"));

    crypto::SecureBytes img;
    REQUIRE(v2.read_image(*kids[0], img) == vault::VaultResult::Ok);
    CHECK_BYTES_EQ(img.as_span(), std::span<const uint8_t>(png));

    crypto::SecureBytes thumb;  // the thumbnail chunk must have been carried over
    REQUIRE(kids[0]->meta.thumb_length > 0);
    CHECK_EQ(v2.read_thumbnail(*kids[0], thumb), vault::VaultResult::Ok);
}

// Deleting an image auto-compacts once the waste passes the threshold
// (>= AUTO_COMPACT_MIN_WASTE and >= a quarter of the file).
TEST(remove_image_auto_compacts_past_waste_threshold)
{
    TempVault tv("autocompact");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    const size_t big = vault::Vault::AUTO_COMPACT_MIN_WASTE * 4;  // safely past both gates
    REQUIRE(v.add_image("", pattern(big, 1), "gone.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(2000, 2), "keep.bin") == vault::VaultResult::Ok);

    const uint64_t size_before = size_on_disk(tv.path);
    REQUIRE(v.remove_image("", "gone.bin") == vault::VaultResult::Ok);

    CHECK_EQ(v.wasted_bytes(), 0u);                        // compaction ran
    CHECK_TRUE(size_on_disk(tv.path) + big <= size_before);  // chunk reclaimed

    auto kids = v.list("");
    REQUIRE(kids.size() == 1);
    crypto::SecureBytes out;
    CHECK_EQ(v.read_image(*kids[0], out), vault::VaultResult::Ok);
}

TEST(remove_image_below_threshold_keeps_orphan)
{
    TempVault tv("nocompact");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    REQUIRE(v.add_image("", pattern(4096, 1), "gone.bin") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(4096, 2), "keep.bin") == vault::VaultResult::Ok);
    REQUIRE(v.remove_image("", "gone.bin") == vault::VaultResult::Ok);

    // 4 KiB of waste is far below AUTO_COMPACT_MIN_WASTE: the orphan stays
    // until an explicit compact() (rewriting the vault per tiny delete would
    // cost more I/O than it reclaims).
    CHECK_TRUE(v.wasted_bytes() >= 4096);
}

TEST(compact_requires_unlocked_vault)
{
    TempVault tv("locked");
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
    }
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    CHECK_EQ(v2.compact(), vault::VaultResult::Locked);
    CHECK_EQ(v2.wasted_bytes(), 0u);  // unknown while locked
}
