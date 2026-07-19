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
               ("osv_xg_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec; fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

static std::vector<uint8_t> blob(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 23 + seed);
    return v;
}

static const vault::IndexNode* find_child(const vault::Vault& v, std::string_view gallery,
                                          std::string_view name)
{
    for (const auto* c : v.list(gallery)) if (c->name == name) return c;
    return nullptr;
}

TEST(move_gallery_nested_roundtrip_across_reopen)
{
    using enum vault::VaultResult;
    TempVault sa("src"), da("dst");
    const auto img1 = blob(3000, 1);
    const auto img2 = blob(3000, 2);

    {
        vault::Vault src, dst;
        REQUIRE(vault::Vault::create(sa.str(), bytes("ps"), {}, kKdf, src) == Ok);
        REQUIRE(vault::Vault::create(da.str(), bytes("pd"), {}, kKdf, dst) == Ok);
        REQUIRE(src.create_gallery("Trips/2024") == Ok);
        REQUIRE(src.add_image("Trips", blob(10, 0), "cover.jpg") == Ok); // Phase 46: mixed OK
        REQUIRE(src.add_image("Trips/2024", img1, "a.jpg") == Ok);
        REQUIRE(src.add_image("Trips/2024", img2, "b.jpg") == Ok);

        // Move "Trips" into dst root.
        REQUIRE(vault::transfer_gallery(src, "Trips", dst, "", vault::TransferMode::Move) == Ok);

        // Source subtree gone; destination has it.
        CHECK(find_child(src, "", "Trips") == nullptr);
        REQUIRE(find_child(dst, "", "Trips") != nullptr);
        REQUIRE(find_child(dst, "Trips", "2024") != nullptr);
    }

    // Reopen both: structure + image bytes persisted on the destination, gone from src.
    vault::Vault src2, dst2;
    REQUIRE(vault::Vault::open(sa.str(), src2) == Ok);
    REQUIRE(src2.unlock(bytes("ps"), {}) == Ok);
    REQUIRE(vault::Vault::open(da.str(), dst2) == Ok);
    REQUIRE(dst2.unlock(bytes("pd"), {}) == Ok);

    CHECK(find_child(src2, "", "Trips") == nullptr);
    const auto* a = find_child(dst2, "Trips/2024", "a.jpg");
    REQUIRE(a != nullptr);
    crypto::SecureBytes out;
    REQUIRE(dst2.read_image(*a, out) == Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(img1));
}

TEST(move_gallery_into_existing_parent)
{
    using enum vault::VaultResult;
    TempVault sa("p1"), da("p2");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("Album") == Ok);
    REQUIRE(src.add_image("Album", blob(1000, 4), "x.jpg") == Ok);
    REQUIRE(dst.create_gallery("Library/Music") == Ok);    // Library holds a sub-gallery

    REQUIRE(vault::transfer_gallery(src, "Album", dst, "Library", vault::TransferMode::Move) == Ok);
    CHECK(find_child(dst, "Library", "Album") != nullptr);
    CHECK(find_child(dst, "Library/Album", "x.jpg") != nullptr);
    CHECK(find_child(src, "", "Album") == nullptr);
}

TEST(move_gallery_name_collision_leaves_source_intact)
{
    using enum vault::VaultResult;
    TempVault sa("c1"), da("c2");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("Dupe") == Ok);
    REQUIRE(src.add_image("Dupe", blob(1000, 6), "i.jpg") == Ok);
    REQUIRE(dst.create_gallery("Dupe") == Ok);   // dst root already has "Dupe"

    CHECK(vault::transfer_gallery(src, "Dupe", dst, "", vault::TransferMode::Move) == AlreadyExists);
    CHECK(find_child(src, "", "Dupe") != nullptr);   // source untouched
}

TEST(move_gallery_into_image_holding_parent_succeeds)
{
    using enum vault::VaultResult;
    TempVault sa("e1"), da("e2");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("G") == Ok);
    REQUIRE(dst.add_image("", blob(1000, 8), "root.jpg") == Ok);   // dst root holds an image

    // Phase 46: a media-holding parent may also gain a sub-gallery.
    CHECK(vault::transfer_gallery(src, "G", dst, "", vault::TransferMode::Move) == Ok);
    CHECK(find_child(src, "", "G") == nullptr);
    CHECK(find_child(dst, "", "G") != nullptr);
    CHECK(find_child(dst, "", "root.jpg") != nullptr);   // original media still present
}

TEST(gallery_target_parents_lists_every_gallery)
{
    using enum vault::VaultResult;
    TempVault tv("targets");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("Empty") == Ok);
    REQUIRE(v.create_gallery("Holder/Inner") == Ok);
    REQUIRE(v.create_gallery("Photos") == Ok);
    REQUIRE(v.add_image("Photos", blob(500, 1), "p.jpg") == Ok);

    auto t = vault::gallery_target_parents(v);
    auto has = [&](std::string_view s) {
        for (const auto& g : t) if (g == s) return true;
        return false;
    };
    // Phase 46: every gallery — including a media holder — is a valid sub-gallery parent.
    CHECK(has(""));
    CHECK(has("Empty"));
    CHECK(has("Holder"));
    CHECK(has("Holder/Inner"));
    CHECK(has("Photos"));   // holds media, now still a valid parent
}

TEST(transfer_gallery_copy_keeps_source_cross_vault)
{
    using enum vault::VaultResult;
    TempVault sa("gc1"), da("gc2");
    const auto img = blob(3000, 5);
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("Album") == Ok);
    REQUIRE(src.add_image("Album", img, "x.jpg") == Ok);

    REQUIRE(vault::transfer_gallery(src, "Album", dst, "", vault::TransferMode::Copy) == Ok);
    CHECK(find_child(src, "", "Album") != nullptr);  // source subtree preserved
    const auto* d = find_child(dst, "Album", "x.jpg");
    REQUIRE(d != nullptr);
    crypto::SecureBytes out;
    REQUIRE(dst.read_image(*d, out) == Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(img));
}

TEST(transfer_gallery_same_vault_move_to_other_parent)
{
    using enum vault::VaultResult;
    TempVault tv("gsv");
    const auto img = blob(2500, 3);
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("Src/Inner") == Ok);
    REQUIRE(v.add_image("Src/Inner", img, "p.jpg") == Ok);
    REQUIRE(v.create_gallery("Dest") == Ok);          // empty → can hold a sub-gallery

    REQUIRE(vault::transfer_gallery(v, "Src", v, "Dest", vault::TransferMode::Move) == Ok);
    CHECK(find_child(v, "", "Src") == nullptr);        // moved out of root
    CHECK(find_child(v, "Dest", "Src") != nullptr);    // now under Dest
    const auto* m = find_child(v, "Dest/Src/Inner", "p.jpg");
    REQUIRE(m != nullptr);
    crypto::SecureBytes out;
    REQUIRE(v.read_image(*m, out) == Ok);
    CHECK_BYTES_EQ(out.as_span(), std::span<const uint8_t>(img));
}

TEST(transfer_gallery_same_vault_into_self_or_descendant_rejected)
{
    using enum vault::VaultResult;
    TempVault tv("gcycle");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("A/B") == Ok);

    CHECK(vault::transfer_gallery(v, "A", v, "A", vault::TransferMode::Move) == InvalidArg);
    CHECK(vault::transfer_gallery(v, "A", v, "A/B", vault::TransferMode::Move) == InvalidArg);
    CHECK(find_child(v, "", "A") != nullptr);          // untouched after rejection
}
