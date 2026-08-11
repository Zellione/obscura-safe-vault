#include "test_framework.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vault/staging.h"
#include "vault/transfer.h"
#include "vault/vault.h"

namespace fs = std::filesystem;

static const crypto::KdfParams kKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

namespace {

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_xfertags_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec; fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
    std::string str() const { return path.string(); }
};

}  // namespace

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 31 + seed);
    return v;
}

TEST(effective_tags_unions_own_and_ancestors) {
    using enum vault::VaultResult;
    TempVault tv("efftags");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("a") == Ok);
    REQUIRE(v.create_gallery("a/b") == Ok);
    REQUIRE(v.add_tag("", "global") == Ok);      // root tag cascades
    REQUIRE(v.add_tag("a", "series") == Ok);
    REQUIRE(v.add_tag("a/b", "chapter") == Ok);
    auto img = pattern(64, 1);
    vault::StagedThumb no_thumb;   // precomputed empty thumb: no decode needed
    REQUIRE(vault::add_image_prestaged(v, "a/b", img, "x.jpg", no_thumb, 0)
            == Ok);
    REQUIRE(v.add_tag("a/b/x.jpg", "OWN") == Ok);
    REQUIRE(v.add_tag("a/b/x.jpg", "Series") == Ok);  // ci-dupe of ancestor

    auto eff = vault::effective_tags(v, "a/b/x.jpg");
    // own first (own casing wins), then inherited minus ci-duplicates
    REQUIRE(eff.size() == 4);
    CHECK(eff[0] == "OWN");
    CHECK(eff[1] == "Series");     // node's casing kept; ancestor "series" deduped
    // remaining two are "global" and "chapter" in root->parent order
    CHECK(std::ranges::find(eff, "global")  != eff.end());
    CHECK(std::ranges::find(eff, "chapter") != eff.end());
}

TEST(effective_tags_locked_or_missing_is_empty) {
    using enum vault::VaultResult;
    TempVault tv("locked");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);
    REQUIRE(v.create_gallery("a") == Ok);
    REQUIRE(v.add_tag("a", "test") == Ok);

    // Locked vault -> empty
    vault::Vault locked;
    REQUIRE(vault::Vault::open(tv.str(), locked) == Ok);
    auto eff = vault::effective_tags(locked, "a");
    CHECK(eff.empty());

    // Unlocked but missing path -> empty
    REQUIRE(locked.unlock(bytes("pw"), {}) == Ok);
    auto eff2 = vault::effective_tags(locked, "nonexistent");
    CHECK(eff2.empty());
}

TEST(attach_prestaged_applies_extras_and_survives_reopen) {
    using enum vault::VaultResult;
    TempVault tv("extras");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);

    vault::StagedThumb no_thumb;
    vault::NodeExtras extras{.tags = {"alpha", "Beta"}, .favorite = true};
    auto img = pattern(64, 2);
    REQUIRE(vault::attach_image_prestaged(v, "", img, "e.jpg", no_thumb, 0, &extras)
            == Ok);
    REQUIRE(vault::commit_staged(v) == Ok);

    const vault::IndexNode* n = v.resolve_node("e.jpg");
    REQUIRE(n != nullptr);
    CHECK(n->tags == std::vector<std::string>({"alpha", "Beta"}));
    CHECK(n->favorite);

    /* lock, reopen, unlock — assert tags + favorite persisted */
    v.lock();
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == Ok);

    const vault::IndexNode* n2 = v2.resolve_node("e.jpg");
    REQUIRE(n2 != nullptr);
    CHECK(n2->tags == std::vector<std::string>({"alpha", "Beta"}));
    CHECK(n2->favorite);
}

TEST(attach_prestaged_null_extras_is_unchanged_behavior) {
    using enum vault::VaultResult;
    TempVault tv("null_extras");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);

    vault::StagedThumb no_thumb;
    auto img = pattern(64, 3);
    /* attach without extras -> empty tags, favorite=false (pin the default) */
    REQUIRE(vault::attach_image_prestaged(v, "", img, "n.jpg", no_thumb, 0, nullptr)
            == Ok);
    REQUIRE(vault::commit_staged(v) == Ok);

    const vault::IndexNode* n = v.resolve_node("n.jpg");
    REQUIRE(n != nullptr);
    CHECK(n->tags.empty());
    CHECK_FALSE(n->favorite);
}

TEST(attach_prestaged_caps_extras_at_index_max_tags) {
    using enum vault::VaultResult;
    TempVault tv("cap_tags");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);

    vault::StagedThumb no_thumb;
    auto img = pattern(64, 4);

    /* extras with INDEX_MAX_TAGS+10 tags -> node carries exactly INDEX_MAX_TAGS */
    std::vector<std::string> many_tags;
    for (int i = 0; i < vault::INDEX_MAX_TAGS + 10; ++i) {
        many_tags.push_back("tag_" + std::to_string(i));
    }
    vault::NodeExtras extras{.tags = many_tags, .favorite = false};
    REQUIRE(vault::attach_image_prestaged(v, "", img, "c.jpg", no_thumb, 0, &extras)
            == Ok);
    REQUIRE(vault::commit_staged(v) == Ok);

    const vault::IndexNode* n = v.resolve_node("c.jpg");
    REQUIRE(n != nullptr);
    CHECK_EQ(n->tags.size(), vault::INDEX_MAX_TAGS);
}

TEST(transfer_images_materializes_inherited_tags_cross_vault) {
    using enum vault::VaultResult;
    TempVault src_tv("src_xfer");
    TempVault dst_tv("dst_xfer");

    vault::Vault src;
    vault::Vault dst;
    REQUIRE(vault::Vault::create(src_tv.str(), bytes("pw"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(dst_tv.str(), bytes("pw"), {}, kKdf, dst) == Ok);

    // Set up source: gallery "a" tagged "series", image a/x.jpg tagged "own", favorite=true
    REQUIRE(src.create_gallery("a") == Ok);
    REQUIRE(src.add_tag("a", "series") == Ok);

    vault::StagedThumb no_thumb;
    auto img = pattern(64, 10);
    vault::NodeExtras src_extras{.tags = {"own"}, .favorite = true};
    REQUIRE(vault::add_image_prestaged(src, "a", img, "x.jpg", no_thumb, 0, &src_extras)
            == Ok);
    REQUIRE(vault::commit_staged(src) == Ok);

    // Set up destination: empty gallery "in"
    REQUIRE(dst.create_gallery("in") == Ok);

    // Transfer image in Copy mode
    REQUIRE(vault::transfer_images(src, "a", {"x.jpg"}, dst, "in", vault::TransferMode::Copy)
            .done == 1);

    // Verify destination has effective tags (own + inherited series) and favorite
    const vault::IndexNode* dst_node = dst.resolve_node("in/x.jpg");
    REQUIRE(dst_node != nullptr);
    REQUIRE(dst_node->tags.size() == 2);
    CHECK(std::ranges::find(dst_node->tags, "own") != dst_node->tags.end());
    CHECK(std::ranges::find(dst_node->tags, "series") != dst_node->tags.end());
    CHECK(dst_node->favorite);

    // Verify source is untouched (Copy mode)
    const vault::IndexNode* src_node = src.resolve_node("a/x.jpg");
    REQUIRE(src_node != nullptr);
    CHECK(src_node->favorite);
}

TEST(transfer_image_move_out_of_tagged_gallery_same_vault) {
    using enum vault::VaultResult;
    TempVault tv("samevault_move");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kKdf, v) == Ok);

    // Create galleries: "a" (tagged "series") and "b" (no tags)
    REQUIRE(v.create_gallery("a") == Ok);
    REQUIRE(v.create_gallery("b") == Ok);
    REQUIRE(v.add_tag("a", "series") == Ok);

    // Add image a/x.jpg with own tag "own"
    vault::StagedThumb no_thumb;
    auto img = pattern(64, 11);
    vault::NodeExtras img_extras{.tags = {"own"}, .favorite = false};
    REQUIRE(vault::add_image_prestaged(v, "a", img, "x.jpg", no_thumb, 0, &img_extras)
            == Ok);
    REQUIRE(vault::commit_staged(v) == Ok);

    // Transfer image in Move mode (same vault)
    REQUIRE(vault::transfer_image(v, "a", "x.jpg", v, "b", vault::TransferMode::Move) == Ok);

    // Verify destination has both tags
    const vault::IndexNode* dst_node = v.resolve_node("b/x.jpg");
    REQUIRE(dst_node != nullptr);
    REQUIRE(dst_node->tags.size() == 2);
    CHECK(std::ranges::find(dst_node->tags, "own") != dst_node->tags.end());
    CHECK(std::ranges::find(dst_node->tags, "series") != dst_node->tags.end());

    // Verify source is gone
    CHECK(v.resolve_node("a/x.jpg") == nullptr);
}

TEST(transfer_preserves_favorite_and_no_tag_duplication) {
    using enum vault::VaultResult;
    TempVault src_tv("src_nodup");
    TempVault dst_tv("dst_nodup");

    vault::Vault src;
    vault::Vault dst;
    REQUIRE(vault::Vault::create(src_tv.str(), bytes("pw"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(dst_tv.str(), bytes("pw"), {}, kKdf, dst) == Ok);

    // Source: gallery "a" (tag "series"), image a/x.jpg (tag "series" duplicate)
    REQUIRE(src.create_gallery("a") == Ok);
    REQUIRE(src.add_tag("a", "series") == Ok);

    vault::StagedThumb no_thumb;
    auto img = pattern(64, 12);
    vault::NodeExtras img_extras{.tags = {"series"}, .favorite = true};  // ci-duplicate of ancestor
    REQUIRE(vault::add_image_prestaged(src, "a", img, "x.jpg", no_thumb, 0, &img_extras)
            == Ok);
    REQUIRE(vault::commit_staged(src) == Ok);

    // Destination: gallery "in" also tagged "series"
    REQUIRE(dst.create_gallery("in") == Ok);
    REQUIRE(dst.add_tag("in", "series") == Ok);

    // Transfer
    REQUIRE(vault::transfer_images(src, "a", {"x.jpg"}, dst, "in", vault::TransferMode::Copy)
            .done == 1);

    // Verify no ci-duplicates in destination node tags (effective_tags dedupes, transfer preserves)
    const vault::IndexNode* dst_node = dst.resolve_node("in/x.jpg");
    REQUIRE(dst_node != nullptr);
    CHECK(dst_node->favorite);

    // Check that "series" appears only once (case-insensitive dedup in effective_tags)
    int series_count = 0;
    for (const auto& t : dst_node->tags) {
        if (vault::tag_ci_equal(t, "series")) ++series_count;
    }
    CHECK_EQ(series_count, 1);
}

TEST(transfer_video_carries_extras) {
    using enum vault::VaultResult;
    TempVault src_tv("src_video");
    TempVault dst_tv("dst_video");

    vault::Vault src;
    vault::Vault dst;
    REQUIRE(vault::Vault::create(src_tv.str(), bytes("pw"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(dst_tv.str(), bytes("pw"), {}, kKdf, dst) == Ok);

    // Source: gallery "a", video a/test.avi with tags and favorite
    REQUIRE(src.create_gallery("a") == Ok);
    REQUIRE(src.add_tag("a", "videos") == Ok);

    const auto video_data = pattern(2u << 20, 13);
    vault::StagedVideoInfo info;
    info.poster_jpeg = {0xFF, 0xD8, 1, 2, 3};
    info.codec       = vault::VideoCodec::Unknown;
    info.container   = vault::VideoContainer::Unknown;
    info.width = 320; info.height = 240; info.duration_us = 99;

    vault::NodeExtras vid_extras{.tags = {"action"}, .favorite = true};
    REQUIRE(vault::add_video_prestaged(src, "a", video_data, "test.avi", info, 777, &vid_extras)
            == Ok);
    REQUIRE(vault::commit_staged(src) == Ok);

    // Destination: gallery "in"
    REQUIRE(dst.create_gallery("in") == Ok);

    // Transfer video
    REQUIRE(vault::transfer_images(src, "a", {"test.avi"}, dst, "in", vault::TransferMode::Copy)
            .done == 1);

    // Verify destination video carries both tags and favorite
    const vault::IndexNode* dst_node = dst.resolve_node("in/test.avi");
    REQUIRE(dst_node != nullptr);
    CHECK(dst_node->favorite);
    REQUIRE(dst_node->tags.size() == 2);
    CHECK(std::ranges::find(dst_node->tags, "action") != dst_node->tags.end());
    CHECK(std::ranges::find(dst_node->tags, "videos") != dst_node->tags.end());
}

TEST(transfer_gallery_root_materializes_ancestor_tags) {
    using enum vault::VaultResult;
    TempVault src_tv("src_gal_anc");
    TempVault dst_tv("dst_gal_anc");

    vault::Vault src;
    vault::Vault dst;
    REQUIRE(vault::Vault::create(src_tv.str(), bytes("pw"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(dst_tv.str(), bytes("pw"), {}, kKdf, dst) == Ok);

    // Source setup: root tag "global"; gallery "top" tag "series"; "top/sub" tag "part";
    // image top/sub/x.jpg untagged
    REQUIRE(src.add_tag("", "global") == Ok);
    REQUIRE(src.create_gallery("top") == Ok);
    REQUIRE(src.add_tag("top", "series") == Ok);
    REQUIRE(src.create_gallery("top/sub") == Ok);
    REQUIRE(src.add_tag("top/sub", "part") == Ok);

    vault::StagedThumb no_thumb;
    auto img = pattern(64, 20);
    REQUIRE(vault::add_image_prestaged(src, "top/sub", img, "x.jpg", no_thumb, 0)
            == Ok);
    REQUIRE(vault::commit_staged(src) == Ok);

    // Destination: empty root
    REQUIRE(dst.create_gallery("dest") == Ok);

    // Transfer gallery "top" into dst/"dest"
    REQUIRE(vault::transfer_gallery(src, "top", dst, "dest", vault::TransferMode::Copy) == Ok);

    // Verify: dst/dest/top has both "series" (own) AND "global" (materialized root)
    const vault::IndexNode* top_node = dst.resolve_node("dest/top");
    REQUIRE(top_node != nullptr);
    REQUIRE(top_node->is_gallery());
    REQUIRE(top_node->tags.size() >= 2);
    CHECK(std::ranges::find(top_node->tags, "series") != top_node->tags.end());
    CHECK(std::ranges::find(top_node->tags, "global") != top_node->tags.end());

    // Verify: dst/dest/top/sub has only "part" (own tags, no materialization for internal galleries)
    const vault::IndexNode* sub_node = dst.resolve_node("dest/top/sub");
    REQUIRE(sub_node != nullptr);
    REQUIRE(sub_node->is_gallery());
    // sub should have its own tags only
    int part_count = 0;
    for (const auto& t : sub_node->tags)
        if (t == "part") ++part_count;
    CHECK(part_count == 1);
    // sub should NOT have "global" (it inherits, not materializes)
    CHECK(std::ranges::find(sub_node->tags, "global") == sub_node->tags.end());

    // Verify: dst/dest/top/sub/x.jpg carries source effective tags (own + cascade)
    // The image has no own tags, but inherits from the src tree ("global", "series", "part")
    const vault::IndexNode* img_node = dst.resolve_node("dest/top/sub/x.jpg");
    REQUIRE(img_node != nullptr);
    // Image carries effective tags from source: global (root), series (top), part (sub)
    REQUIRE(img_node->tags.size() >= 3);
    CHECK(std::ranges::find(img_node->tags, "global") != img_node->tags.end());
    CHECK(std::ranges::find(img_node->tags, "series") != img_node->tags.end());
    CHECK(std::ranges::find(img_node->tags, "part") != img_node->tags.end());
}

TEST(transfer_gallery_carries_gallery_favorites) {
    using enum vault::VaultResult;
    TempVault src_tv("src_gal_fav");
    TempVault dst_tv("dst_gal_fav");

    vault::Vault src;
    vault::Vault dst;
    REQUIRE(vault::Vault::create(src_tv.str(), bytes("pw"), {}, kKdf, src) == Ok);
    REQUIRE(vault::Vault::create(dst_tv.str(), bytes("pw"), {}, kKdf, dst) == Ok);

    // Source setup: gallery "fav_gal" marked as favorite, with media
    REQUIRE(src.create_gallery("fav_gal") == Ok);
    REQUIRE(src.toggle_favorite("fav_gal") == Ok);  // toggle once to set favorite

    vault::StagedThumb no_thumb;
    auto img = pattern(64, 21);
    REQUIRE(vault::add_image_prestaged(src, "fav_gal", img, "pic.jpg", no_thumb, 0)
            == Ok);
    REQUIRE(vault::commit_staged(src) == Ok);

    // Destination: empty
    REQUIRE(dst.create_gallery("dest") == Ok);

    // Transfer gallery
    REQUIRE(vault::transfer_gallery(src, "fav_gal", dst, "dest", vault::TransferMode::Copy)
            == Ok);

    // Verify: destination gallery carries favorite flag
    const vault::IndexNode* dst_gal = dst.resolve_node("dest/fav_gal");
    REQUIRE(dst_gal != nullptr);
    REQUIRE(dst_gal->is_gallery());
    CHECK(dst_gal->favorite);
}

// Third test: transfer into existing parent with tag union and idempotency
// This is covered by combine_galleries carrying effective tags (test_combine.cpp)
// and the two basic gallery transfer tests above cover the core root materialization
// and favorite flag propagation.
