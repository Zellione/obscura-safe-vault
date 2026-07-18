#include "test_framework.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vault/combine.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

static const crypto::KdfParams kKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

static std::vector<uint8_t> blob(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 19 + seed);
    return v;
}

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_combine_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec; fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

static const vault::IndexNode* find_child(const vault::Vault& v, std::string_view gallery,
                                          std::string_view name)
{
    for (const auto* c : v.list(gallery)) if (c->name == name) return c;
    return nullptr;
}

static bool has_tag(const vault::IndexNode* n, std::string_view tag)
{
    for (const auto& t : n->tags) if (t == tag) return true;
    return false;
}

TEST(combine_leaf_merges_media_and_deletes_source)
{
    using enum vault::VaultResult;
    TempVault tv("leaf");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("A") == Ok);
    REQUIRE(v.create_gallery("B") == Ok);
    REQUIRE(v.add_image("A", blob(500, 1), "x.jpg") == Ok);
    REQUIRE(v.add_image("A", blob(500, 2), "y.jpg") == Ok);
    REQUIRE(v.add_image("B", blob(500, 3), "z.jpg") == Ok);

    vault::CombineTally tally;
    CHECK(vault::combine_galleries(v, "A", v, "B", tally) == Ok);
    CHECK(tally.media_moved == 2);
    CHECK(tally.media_skipped == 0);
    CHECK(find_child(v, "", "A") == nullptr);         // source gone
    CHECK(find_child(v, "B", "x.jpg") != nullptr);
    CHECK(find_child(v, "B", "y.jpg") != nullptr);
    CHECK(find_child(v, "B", "z.jpg") != nullptr);    // pre-existing dst content untouched
}

TEST(combine_leaf_collision_is_skipped_and_source_survives_partial)
{
    using enum vault::VaultResult;
    TempVault tv("collide");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("A") == Ok);
    REQUIRE(v.create_gallery("B") == Ok);
    REQUIRE(v.add_image("A", blob(500, 1), "dup.jpg") == Ok);
    REQUIRE(v.add_image("A", blob(500, 2), "unique.jpg") == Ok);
    REQUIRE(v.add_image("B", blob(500, 9), "dup.jpg") == Ok);   // collides with A's dup.jpg

    vault::CombineTally tally;
    CHECK(vault::combine_galleries(v, "A", v, "B", tally) == Ok);
    CHECK(tally.media_moved == 1);
    CHECK(tally.media_skipped == 1);
    CHECK(find_child(v, "", "A") != nullptr);            // NOT fully emptied -> source survives
    CHECK(find_child(v, "A", "dup.jpg") != nullptr);      // the skipped one stays behind
    CHECK(find_child(v, "A", "unique.jpg") == nullptr);   // the successful one moved
    CHECK(find_child(v, "B", "unique.jpg") != nullptr);
}

TEST(combine_folder_recurses_into_same_named_child_and_moves_the_rest_wholesale)
{
    using enum vault::VaultResult;
    TempVault tv("folder");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("Src/Shared") == Ok);
    REQUIRE(v.create_gallery("Src/OnlyInSrc") == Ok);
    REQUIRE(v.create_gallery("Dst/Shared") == Ok);
    REQUIRE(v.add_image("Src/Shared", blob(400, 1), "a.jpg") == Ok);
    REQUIRE(v.add_image("Dst/Shared", blob(400, 2), "b.jpg") == Ok);
    REQUIRE(v.add_image("Src/OnlyInSrc", blob(400, 3), "c.jpg") == Ok);

    vault::CombineTally tally;
    CHECK(vault::combine_galleries(v, "Src", v, "Dst", tally) == Ok);
    CHECK(find_child(v, "", "Src") == nullptr);                    // fully merged away
    CHECK(tally.galleries_merged == 1);   // "Shared" recursed
    CHECK(tally.galleries_moved == 1);    // "OnlyInSrc" moved wholesale
    CHECK(find_child(v, "Dst/Shared", "a.jpg") != nullptr);         // recursed-in media
    CHECK(find_child(v, "Dst/Shared", "b.jpg") != nullptr);         // pre-existing dst media
    CHECK(find_child(v, "Dst", "OnlyInSrc") != nullptr);            // moved wholesale, name kept
    CHECK(find_child(v, "Dst/OnlyInSrc", "c.jpg") != nullptr);
}

TEST(combine_type_mismatch_rejected_media_vs_subgalleries)
{
    using enum vault::VaultResult;
    TempVault tv("mismatch");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("Leaf") == Ok);
    REQUIRE(v.add_image("Leaf", blob(300, 1), "x.jpg") == Ok);
    REQUIRE(v.create_gallery("Folder/Inner") == Ok);   // Folder holds a sub-gallery

    vault::CombineTally tally;
    CHECK(vault::combine_galleries(v, "Leaf", v, "Folder", tally) == InvalidArg);
    CHECK(find_child(v, "", "Leaf") != nullptr);   // untouched on rejection
}

TEST(combine_target_galleries_filters_type_mismatches_self_and_descendants)
{
    using enum vault::VaultResult;
    TempVault tv("targets");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("Photos") == Ok);
    REQUIRE(v.add_image("Photos", blob(300, 1), "p.jpg") == Ok);
    REQUIRE(v.create_gallery("Photos/Sub") == InvalidArg);  // Photos holds media, can't also hold a sub-gallery
    REQUIRE(v.create_gallery("OtherLeaf") == Ok);
    REQUIRE(v.create_gallery("Docs/Inner") == Ok);          // Docs holds a sub-gallery

    const auto t = vault::combine_target_galleries(v, v, "Photos");
    auto has = [&](std::string_view s) {
        for (auto& g : t) if (g == s) return true;
        return false;
    };
    CHECK(has("OtherLeaf"));      // type-compatible (media-holding vs empty)
    CHECK_FALSE(has("Photos"));   // self excluded
    CHECK_FALSE(has("Docs"));     // holds a sub-gallery -> incompatible with a media-holding source
    CHECK(has("Docs/Inner"));   // Inner is empty -> type-compatible despite living under a folder-only parent (Docs)
}

TEST(combine_unions_tags_case_insensitively)
{
    using enum vault::VaultResult;
    TempVault tv("tags");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("A") == Ok);
    REQUIRE(v.create_gallery("B") == Ok);
    REQUIRE(v.set_tags("A", {"Vacation", "Sunny"}) == Ok);
    REQUIRE(v.set_tags("B", {"sunny", "Family"}) == Ok);   // "sunny" collides case-insensitively

    vault::CombineTally tally;
    CHECK(vault::combine_galleries(v, "A", v, "B", tally) == Ok);
    const auto* b = find_child(v, "", "B");
    REQUIRE(b != nullptr);
    CHECK(b->tags.size() == 3);   // Vacation, sunny (kept dst's casing), Family
    CHECK(has_tag(b, "Family"));
    CHECK(has_tag(b, "Vacation"));
    CHECK(has_tag(b, "sunny"));
}

TEST(combine_cross_vault_merges_and_removes_source)
{
    using enum vault::VaultResult;
    TempVault sa("cvsrc"), da("cvdst");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("Trip") == Ok);
    REQUIRE(dst.create_gallery("Trip") == Ok);
    REQUIRE(src.add_image("Trip", blob(400, 1), "a.jpg") == Ok);

    vault::CombineTally tally;
    CHECK(vault::combine_galleries(src, "Trip", dst, "Trip", tally) == Ok);
    CHECK(find_child(src, "", "Trip") == nullptr);
    CHECK(find_child(dst, "Trip", "a.jpg") != nullptr);
}

TEST(combine_self_or_descendant_destination_rejected_same_vault)
{
    using enum vault::VaultResult;
    TempVault tv("cycle");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("A/B") == Ok);

    vault::CombineTally t1, t2;
    CHECK(vault::combine_galleries(v, "A", v, "A", t1) == InvalidArg);
    CHECK(vault::combine_galleries(v, "A", v, "A/B", t2) == InvalidArg);
    CHECK(find_child(v, "", "A") != nullptr);
}

TEST(combine_root_source_rejected)
{
    using enum vault::VaultResult;
    TempVault tv("rootsrc");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("X") == Ok);
    vault::CombineTally tally;
    CHECK(vault::combine_galleries(v, "", v, "X", tally) == InvalidArg);
}

TEST(combine_missing_source_returns_not_found)
{
    using enum vault::VaultResult;
    TempVault tv("nfsrc");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("Real") == Ok);
    vault::CombineTally tally;
    CHECK(vault::combine_galleries(v, "Ghost", v, "Real", tally) == NotFound);
}
