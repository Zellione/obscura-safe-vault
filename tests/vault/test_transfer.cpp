#include "test_framework.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vault/transfer.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

static const crypto::KdfParams kKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

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
               ("osv_xfer_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec; fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 31 + seed);
    return v;
}

// Find an image node by name in a gallery; nullptr if absent.
static const vault::IndexNode* find_image(const vault::Vault& v, std::string_view gallery,
                                          std::string_view name)
{
    for (const auto* c : v.list(gallery))
        if (c->is_image() && c->name == name) return c;
    return nullptr;
}

TEST(transfer_move_roundtrip_across_reopen)
{
    using enum vault::VaultResult;
    TempVault sa("src"), da("dst");
    const auto img = pattern(50000, 7);

    {
        vault::Vault src, dst;
        REQUIRE(vault::Vault::create(sa.str(), bytes("pw-src"), {}, kKdf, src) == Ok);
        REQUIRE(vault::Vault::create(da.str(), bytes("pw-dst"), {}, kKdf, dst) == Ok);
        REQUIRE(src.add_image("", img, "cat.jpg") == Ok);

        // Two vaults unlocked at once for the move.
        REQUIRE(vault::transfer_image(src, "", "cat.jpg", dst, "", vault::TransferMode::Move) == Ok);

        // Gone from source, present in destination (this session).
        CHECK(find_image(src, "", "cat.jpg") == nullptr);
        const auto* moved = find_image(dst, "", "cat.jpg");
        REQUIRE(moved != nullptr);
    }

    // Reopen both: the move persisted on both sides.
    vault::Vault src2, dst2;
    REQUIRE(vault::Vault::open(sa.str(), src2) == Ok);
    REQUIRE(src2.unlock(bytes("pw-src"), {}) == Ok);
    REQUIRE(vault::Vault::open(da.str(), dst2) == Ok);
    REQUIRE(dst2.unlock(bytes("pw-dst"), {}) == Ok);

    CHECK(find_image(src2, "", "cat.jpg") == nullptr);
    const auto* moved = find_image(dst2, "", "cat.jpg");
    REQUIRE(moved != nullptr);

    crypto::SecureBytes out;
    REQUIRE(dst2.read_image(*moved, out) == Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(img));
}

TEST(transfer_move_into_subgallery)
{
    using enum vault::VaultResult;
    TempVault sa("src2"), da("dst2");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.add_image("", pattern(1000, 3), "a.png") == Ok);
    REQUIRE(dst.create_gallery("Trips") == Ok);

    REQUIRE(vault::transfer_image(src, "", "a.png", dst, "Trips", vault::TransferMode::Move) == Ok);
    CHECK(find_image(dst, "Trips", "a.png") != nullptr);
    CHECK(find_image(src, "", "a.png") == nullptr);
}

TEST(transfer_move_missing_source_returns_not_found)
{
    using enum vault::VaultResult;
    TempVault sa("src3"), da("dst3");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    CHECK(vault::transfer_image(src, "", "nope.jpg", dst, "", vault::TransferMode::Move) == NotFound);
}

TEST(transfer_move_name_collision_leaves_source_intact)
{
    using enum vault::VaultResult;
    TempVault sa("src4"), da("dst4");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.add_image("", pattern(1000, 1), "dup.jpg") == Ok);
    REQUIRE(dst.add_image("", pattern(1000, 9), "dup.jpg") == Ok);

    CHECK(vault::transfer_image(src, "", "dup.jpg", dst, "", vault::TransferMode::Move) == AlreadyExists);
    // A failed add must not remove the source image.
    CHECK(find_image(src, "", "dup.jpg") != nullptr);
}

TEST(transfer_move_into_non_leaf_returns_invalid_arg)
{
    using enum vault::VaultResult;
    TempVault sa("src5"), da("dst5");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.add_image("", pattern(1000, 1), "x.jpg") == Ok);
    REQUIRE(dst.create_gallery("Parent/Child") == Ok);   // root now holds a sub-gallery

    // Root of dst holds a sub-gallery, so it can't accept images.
    CHECK(vault::transfer_image(src, "", "x.jpg", dst, "", vault::TransferMode::Move) == InvalidArg);
    CHECK(find_image(src, "", "x.jpg") != nullptr);
}

TEST(transfer_image_target_galleries_lists_leaves_and_eligible_root)
{
    using enum vault::VaultResult;
    TempVault da("targets");
    vault::Vault v;
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("A") == Ok);          // empty leaf
    REQUIRE(v.create_gallery("B/Inner") == Ok);    // B holds a sub-gallery; Inner is a leaf

    auto t = vault::image_target_galleries(v);
    // Root holds sub-galleries (A, B) -> NOT eligible. Eligible: A, B/Inner.
    auto has = [&](std::string_view s) {
        for (auto& g : t)
            if (g == s) return true;
        return false;
    };
    CHECK(has("A"));
    CHECK(has("B/Inner"));
    CHECK_FALSE(has(""));     // root has sub-galleries
    CHECK_FALSE(has("B"));    // B holds a sub-gallery
}

TEST(transfer_image_target_galleries_root_eligible_when_empty)
{
    using enum vault::VaultResult;
    TempVault da("targets2");
    vault::Vault v;
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, v) == Ok);
    auto t = vault::image_target_galleries(v);
    bool has_root = false;
    for (auto& g : t) if (g.empty()) has_root = true;
    CHECK(has_root);   // empty root can accept images
}

TEST(transfer_image_copy_keeps_source_cross_vault)
{
    using enum vault::VaultResult;
    TempVault sa("cpsrc"), da("cpdst");
    const auto img = pattern(20000, 5);
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.add_image("", img, "c.jpg") == Ok);

    REQUIRE(vault::transfer_image(src, "", "c.jpg", dst, "", vault::TransferMode::Copy) == Ok);

    // Present in BOTH after a Copy; bytes match on each side.
    const auto* s = find_image(src, "", "c.jpg");
    const auto* d = find_image(dst, "", "c.jpg");
    REQUIRE(s != nullptr);
    REQUIRE(d != nullptr);
    crypto::SecureBytes so, do_;
    REQUIRE(src.read_image(*s, so) == Ok);
    REQUIRE(dst.read_image(*d, do_) == Ok);
    CHECK_BYTES_EQ(so.as_span(), std::span<const uint8_t>(img));
    CHECK_BYTES_EQ(do_.as_span(), std::span<const uint8_t>(img));
}

TEST(transfer_image_same_vault_move_between_galleries)
{
    using enum vault::VaultResult;
    TempVault tv("svmove");
    const auto img = pattern(8000, 4);
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("A") == Ok);
    REQUIRE(v.create_gallery("B") == Ok);
    REQUIRE(v.add_image("A", img, "x.jpg") == Ok);

    REQUIRE(vault::transfer_image(v, "A", "x.jpg", v, "B", vault::TransferMode::Move) == Ok);

    CHECK(find_image(v, "A", "x.jpg") == nullptr);   // gone from A
    const auto* m = find_image(v, "B", "x.jpg");
    REQUIRE(m != nullptr);                            // present in B
    crypto::SecureBytes out;
    REQUIRE(v.read_image(*m, out) == Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(img));
}

TEST(transfer_image_same_vault_copy_between_galleries)
{
    using enum vault::VaultResult;
    TempVault tv("svcopy");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("A") == Ok);
    REQUIRE(v.create_gallery("B") == Ok);
    REQUIRE(v.add_image("A", pattern(4000, 2), "y.png") == Ok);

    REQUIRE(vault::transfer_image(v, "A", "y.png", v, "B", vault::TransferMode::Copy) == Ok);
    CHECK(find_image(v, "A", "y.png") != nullptr);   // still in A
    CHECK(find_image(v, "B", "y.png") != nullptr);   // and now in B
}
