#include "test_framework.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vault/combine.h"
#include "vault/file_util.h"
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

// Internal linkage: several vault test files each define their own `TempVault`
// with a DIFFERENT layout. At namespace scope those are one-definition-rule
// violations — the member functions are implicitly inline, so the linker keeps
// a single copy and silently discards the rest.
namespace {

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

}  // namespace

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

TEST(combine_media_into_subgallery_holder_merges)
{
    using enum vault::VaultResult;
    TempVault tv("mixmerge");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("Leaf") == Ok);
    REQUIRE(v.add_image("Leaf", blob(300, 1), "x.jpg") == Ok);
    REQUIRE(v.create_gallery("Folder/Inner") == Ok);   // Folder holds a sub-gallery

    // Phase 46: media source merges into a sub-gallery-holding destination.
    vault::CombineTally tally;
    CHECK(vault::combine_galleries(v, "Leaf", v, "Folder", tally) == Ok);
    CHECK(find_child(v, "Folder", "x.jpg") != nullptr);   // media landed alongside "Inner"
    CHECK(find_child(v, "Folder", "Inner") != nullptr);   // pre-existing sub-gallery intact
    CHECK(find_child(v, "", "Leaf") == nullptr);          // emptied source removed
}

TEST(combine_target_galleries_lists_all_but_self_and_descendants)
{
    using enum vault::VaultResult;
    TempVault tv("targets");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("Photos") == Ok);
    REQUIRE(v.add_image("Photos", blob(300, 1), "p.jpg") == Ok);
    REQUIRE(v.create_gallery("Photos/Sub") == Ok);   // Phase 46: Photos now holds media AND a sub-gallery
    REQUIRE(v.create_gallery("OtherLeaf") == Ok);
    REQUIRE(v.create_gallery("Docs/Inner") == Ok);

    const auto t = vault::combine_target_galleries(v, v, "Photos");
    auto has = [&](std::string_view s) {
        for (auto& g : t) {
            if (g == s) {
                return true;
            }
        }
        return false;
    };
    CHECK(has("OtherLeaf"));
    CHECK(has("Docs"));           // Phase 46: folder-holder is now a valid destination
    CHECK(has("Docs/Inner"));
    CHECK_FALSE(has("Photos"));       // self excluded
    CHECK_FALSE(has("Photos/Sub"));   // descendant of source excluded
}

TEST(combine_mixed_source_moves_media_and_subgalleries)
{
    using enum vault::VaultResult;
    TempVault tv("mixsrc");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    // Source holds BOTH a media file and a sub-gallery.
    REQUIRE(v.create_gallery("Src") == Ok);
    REQUIRE(v.add_image("Src", blob(300, 2), "m.jpg") == Ok);
    REQUIRE(v.create_gallery("Src/Child") == Ok);
    REQUIRE(v.create_gallery("Dst") == Ok);

    vault::CombineTally tally;
    CHECK(vault::combine_galleries(v, "Src", v, "Dst", tally) == Ok);
    CHECK(find_child(v, "Dst", "m.jpg") != nullptr);   // media moved
    CHECK(find_child(v, "Dst", "Child") != nullptr);   // sub-gallery moved
    CHECK(find_child(v, "", "Src") == nullptr);        // fully emptied → removed
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

TEST(combine_media_carries_effective_tags)
{
    using enum vault::VaultResult;
    TempVault tv("combine_media_tags");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);

    // Source: root (tag "global"), gallery "Src" (tag "series"), image Src/pic.jpg (tag "own")
    REQUIRE(v.add_tag("", "global") == Ok);
    REQUIRE(v.create_gallery("Src") == Ok);
    REQUIRE(v.add_tag("Src", "series") == Ok);
    REQUIRE(v.add_image("Src", blob(300, 1), "pic.jpg") == Ok);
    REQUIRE(v.add_tag("Src/pic.jpg", "own") == Ok);

    // Destination: gallery "Dst"
    REQUIRE(v.create_gallery("Dst") == Ok);

    // Combine: move Src into Dst
    vault::CombineTally tally;
    CHECK(vault::combine_galleries(v, "Src", v, "Dst", tally) == Ok);

    // Verify: pic.jpg at Dst/pic.jpg now carries effective tags:
    // own (media's own) + series (parent gallery) + global (root)
    const auto* pic = find_child(v, "Dst", "pic.jpg");
    REQUIRE(pic != nullptr);
    REQUIRE(pic->tags.size() >= 3);
    CHECK(has_tag(pic, "own"));
    CHECK(has_tag(pic, "series"));
    CHECK(has_tag(pic, "global"));
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

// Wholesale-moved subtrees count their media in the tally (Phase 69 follow-up):
// the "N moved" completion line must include files that moved inside a
// wholesale transfer_gallery, not only leaf-merged ones.
TEST(combine_counts_wholesale_moved_media_in_tally)
{
    using enum vault::VaultResult;
    TempVault tv("wholetally");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("Src/Only") == Ok);
    REQUIRE(v.create_gallery("Dst") == Ok);
    REQUIRE(v.add_image("Src", blob(300, 1), "leaf.jpg") == Ok);
    REQUIRE(v.add_image("Src/Only", blob(300, 2), "w1.jpg") == Ok);
    REQUIRE(v.add_image("Src/Only", blob(300, 3), "w2.jpg") == Ok);

    vault::CombineTally tally;
    REQUIRE(vault::combine_galleries(v, "Src", v, "Dst", tally) == Ok);

    CHECK(tally.media_moved == 3);     // 1 leaf-merged + 2 wholesale-moved
    CHECK(tally.media_skipped == 0);
    CHECK(tally.galleries_moved == 1);
    CHECK(find_child(v, "Dst/Only", "w1.jpg") != nullptr);
    CHECK(find_child(v, "Dst/Only", "w2.jpg") != nullptr);
}

// Per-file failures inside a wholesale subtree move must surface in the
// combine tally as skipped, not vanish: a failed destination commit fails the
// batch, the files stay in the source, and the tally reports them.
TEST(combine_failed_wholesale_move_counts_media_as_skipped)
{
    using enum vault::VaultResult;
    TempVault tv("wholefail");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("p"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("Src/Only") == Ok);
    REQUIRE(v.create_gallery("Dst") == Ok);
    REQUIRE(v.add_image("Src/Only", blob(300, 1), "a.jpg") == Ok);
    REQUIRE(v.add_image("Src/Only", blob(300, 2), "b.jpg") == Ok);

    // The next fileutil::sync is the wholesale move's batched destination
    // commit — fail it, so both files fail the batch.
    vault::fileutil::inject_sync_failure(0);
    vault::CombineTally tally;
    const vault::VaultResult r = vault::combine_galleries(v, "Src", v, "Dst", tally);
    vault::fileutil::clear_sync_failure();

    REQUIRE(r == Ok);                  // per-item tolerance: combine reports, not aborts
    CHECK(tally.media_moved == 0);
    CHECK(tally.media_skipped == 2);   // the failed batch is accounted for
    CHECK(find_child(v, "Src/Only", "a.jpg") != nullptr);   // still in the source
    CHECK(find_child(v, "Src/Only", "b.jpg") != nullptr);
}

// Copy-mode combine: destination gains the missing files, source untouched.
TEST(combine_copy_mode_leaves_source_intact)
{
    using enum vault::VaultResult;
    TempVault sa("cc_s"), da("cc_d");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("G") == Ok);
    REQUIRE(src.add_image("G", blob(2000, 1), "a.jpg") == Ok);
    REQUIRE(src.add_image("G", blob(2000, 2), "b.jpg") == Ok);
    REQUIRE(dst.create_gallery("G") == Ok);
    REQUIRE(dst.add_image("G", blob(10, 3), "b.jpg") == Ok);   // collision -> skip

    vault::CombineTally t;
    REQUIRE(vault::combine_galleries(src, "G", dst, "G", t, nullptr,
                                     vault::TransferMode::Copy) == Ok);
    CHECK_EQ(t.media_moved, 1);
    CHECK_EQ(t.media_skipped, 1);
    // Source keeps EVERYTHING (copy) — including its gallery shell.
    CHECK(find_child(src, "", "G") != nullptr);
    CHECK(find_child(src, "G", "a.jpg") != nullptr);
    CHECK(find_child(src, "G", "b.jpg") != nullptr);
    CHECK(find_child(dst, "G", "a.jpg") != nullptr);
}

// Copy-mode combine of an EMPTY source gallery must not delete the source
// (the Move-mode "delete once empty" rule is gated on mode).
TEST(combine_copy_mode_never_deletes_empty_source)
{
    using enum vault::VaultResult;
    TempVault sa("ce_s"), da("ce_d");
    vault::Vault src, dst;
    REQUIRE(vault::Vault::create(sa.str(), bytes("p"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(da.str(), bytes("p"), {}, kKdf, dst) == Ok);
    REQUIRE(src.create_gallery("E") == Ok);
    REQUIRE(dst.create_gallery("E") == Ok);
    vault::CombineTally t;
    REQUIRE(vault::combine_galleries(src, "E", dst, "E", t, nullptr,
                                     vault::TransferMode::Copy) == Ok);
    CHECK(find_child(src, "", "E") != nullptr);
}
