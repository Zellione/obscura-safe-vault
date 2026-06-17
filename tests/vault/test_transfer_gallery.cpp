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
        REQUIRE(src.add_image("Trips", blob(10, 0), "cover.jpg") == InvalidArg); // Trips holds a sub-gallery
        REQUIRE(src.add_image("Trips/2024", img1, "a.jpg") == Ok);
        REQUIRE(src.add_image("Trips/2024", img2, "b.jpg") == Ok);

        // Move "Trips" into dst root.
        REQUIRE(vault::move_gallery(src, "Trips", dst, "") == Ok);

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

    REQUIRE(vault::move_gallery(src, "Album", dst, "Library") == Ok);
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

    CHECK(vault::move_gallery(src, "Dupe", dst, "") == AlreadyExists);
    CHECK(find_child(src, "", "Dupe") != nullptr);   // source untouched
}

TEST(move_gallery_into_image_holding_parent_rejected)
{
    using enum vault::VaultResult;
    TempVault sa("e1"), da("e2");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("G") == Ok);
    REQUIRE(dst.add_image("", blob(1000, 8), "root.jpg") == Ok);   // dst root holds an image

    CHECK(vault::move_gallery(src, "G", dst, "") == InvalidArg);
    CHECK(find_child(src, "", "G") != nullptr);
}

TEST(gallery_target_parents_lists_image_free_galleries_and_root)
{
    using enum vault::VaultResult;
    TempVault tv("targets");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("Empty") == Ok);          // can accept a sub-gallery
    REQUIRE(v.create_gallery("Holder/Inner") == Ok);   // Holder holds a sub-gallery → ok
    REQUIRE(v.create_gallery("Photos") == Ok);
    REQUIRE(v.add_image("Photos", blob(500, 1), "p.jpg") == Ok);  // Photos holds images → NOT ok

    auto t = vault::gallery_target_parents(v);
    auto has = [&](std::string_view s) {
        for (auto& g : t) if (g == s) return true; return false;
    };
    CHECK(has(""));            // root holds only sub-galleries
    CHECK(has("Empty"));
    CHECK(has("Holder"));
    CHECK(has("Holder/Inner"));
    CHECK_FALSE(has("Photos")); // holds images
}
