#include "test_framework.h"

#include <algorithm>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "vault/vault.h"

namespace fs = std::filesystem;

// --- helpers --------------------------------------------------------------

// Cheap Argon2 params so tests stay fast.
static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

// RAII temp path: a unique .osv file removed when the helper goes out of scope.
struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_tags_test_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    std::string str() const { return path.string(); }
};

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

// --- tests ----------------------------------------------------------------

TEST(tags_set_tags_on_image_persists_across_reopen)
{
    TempVault tv("image_persist");
    auto img = pattern(5000, 11);

    // Create, add image, set tags.
    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", img, "photo.jpg") == vault::VaultResult::Ok);

        auto children = v.list("");
        REQUIRE(children.size() == 1);
        REQUIRE(children[0]->is_image());

        // Set tags on the image.
        std::vector<std::string> tags{"beach", "sunset", "2024"};
        REQUIRE(v.set_tags("photo.jpg", tags) == vault::VaultResult::Ok);

        // Verify tags were set in-memory.
        auto updated = v.list("");
        REQUIRE(updated[0]->tags.size() == 3);
        CHECK_EQ(updated[0]->tags[0], "beach");
        CHECK_EQ(updated[0]->tags[1], "sunset");
        CHECK_EQ(updated[0]->tags[2], "2024");

        v.lock();
    }

    // Reopen and verify tags survived.
    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    auto children = v2.list("");
    REQUIRE(children.size() == 1);
    REQUIRE(children[0]->tags.size() == 3);
    CHECK_EQ(children[0]->tags[0], "beach");
    CHECK_EQ(children[0]->tags[1], "sunset");
    CHECK_EQ(children[0]->tags[2], "2024");
}

TEST(tags_set_tags_on_gallery_persists)
{
    TempVault tv("gallery_persist");

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.create_gallery("vacation") == vault::VaultResult::Ok);

        // Set tags on the gallery.
        std::vector<std::string> tags{"holiday", "family"};
        REQUIRE(v.set_tags("vacation", tags) == vault::VaultResult::Ok);

        auto root_children = v.list("");
        REQUIRE(root_children.size() == 1);
        REQUIRE(root_children[0]->is_gallery());
        REQUIRE(root_children[0]->tags.size() == 2);

        v.lock();
    }

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    auto root_children = v2.list("");
    REQUIRE(root_children.size() == 1);
    REQUIRE(root_children[0]->tags.size() == 2);
    CHECK_EQ(root_children[0]->tags[0], "holiday");
    CHECK_EQ(root_children[0]->tags[1], "family");
}

TEST(tags_add_tag_idempotent_case_insensitive)
{
    TempVault tv("add_tag");
    auto img = pattern(1000, 5);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", img, "pic.jpg") == vault::VaultResult::Ok);

    // Add a tag.
    REQUIRE(v.add_tag("pic.jpg", "trip") == vault::VaultResult::Ok);
    auto children = v.list("");
    REQUIRE(children[0]->tags.size() == 1);
    CHECK_EQ(children[0]->tags[0], "trip");

    // Add the same tag with different case — should be idempotent.
    REQUIRE(v.add_tag("pic.jpg", "TRIP") == vault::VaultResult::Ok);
    children = v.list("");
    REQUIRE(children[0]->tags.size() == 1);
    CHECK_EQ(children[0]->tags[0], "trip");  // Original casing preserved.

    // Add a different tag.
    REQUIRE(v.add_tag("pic.jpg", "vacation") == vault::VaultResult::Ok);
    children = v.list("");
    REQUIRE(children[0]->tags.size() == 2);
}

TEST(tags_remove_tag_case_insensitive_idempotent)
{
    TempVault tv("remove_tag");
    auto img = pattern(1000, 5);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", img, "pic.jpg") == vault::VaultResult::Ok);

    // Add tags.
    REQUIRE(v.add_tag("pic.jpg", "beach") == vault::VaultResult::Ok);
    REQUIRE(v.add_tag("pic.jpg", "sunset") == vault::VaultResult::Ok);
    auto children = v.list("");
    REQUIRE(children[0]->tags.size() == 2);

    // Remove a tag case-insensitively.
    REQUIRE(v.remove_tag("pic.jpg", "BEACH") == vault::VaultResult::Ok);
    children = v.list("");
    REQUIRE(children[0]->tags.size() == 1);
    CHECK_EQ(children[0]->tags[0], "sunset");

    // Remove a tag that doesn't exist — should be idempotent/Ok.
    REQUIRE(v.remove_tag("pic.jpg", "beach") == vault::VaultResult::Ok);
    children = v.list("");
    REQUIRE(children[0]->tags.size() == 1);

    // Remove the remaining tag.
    REQUIRE(v.remove_tag("pic.jpg", "sunset") == vault::VaultResult::Ok);
    children = v.list("");
    REQUIRE(children[0]->tags.size() == 0);
}

TEST(tags_set_tags_normalisation)
{
    TempVault tv("normalise");
    auto img = pattern(1000, 5);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", img, "pic.jpg") == vault::VaultResult::Ok);

    // Input has whitespace, empties, and case-insensitive duplicates.
    std::vector<std::string> input{" Beach ", "", "beach", "Sunset", "  "};
    REQUIRE(v.set_tags("pic.jpg", input) == vault::VaultResult::Ok);

    auto children = v.list("");
    // Expected: "Beach" (first occurrence casing), "Sunset" (trimmed).
    // Case-insensitive dedup: only one "beach" variant.
    // Empties/whitespace dropped.
    REQUIRE(children[0]->tags.size() == 2);
    CHECK_EQ(children[0]->tags[0], "Beach");
    CHECK_EQ(children[0]->tags[1], "Sunset");
}

// Normalisation trims SURROUNDING whitespace only — interior spaces are part of
// the tag (so multi-word tags like "new york" survive). The tag editor relies
// on this contract rather than stripping spaces itself.
TEST(tags_preserve_interior_whitespace)
{
    TempVault tv("interior_ws");
    auto img = pattern(1000, 5);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", img, "pic.jpg") == vault::VaultResult::Ok);

    REQUIRE(v.add_tag("pic.jpg", "  new york  ") == vault::VaultResult::Ok);

    auto children = v.list("");
    REQUIRE(children[0]->tags.size() == 1);
    CHECK_EQ(children[0]->tags[0], "new york");  // surrounding trimmed, interior kept
}

TEST(tags_tag_cascade_global)
{
    TempVault tv("cascade_global");
    auto img = pattern(1000, 5);

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);

        // Tag the root (unnamed root gallery with empty path "").
        // We'll search to verify this becomes a global tag.
        std::vector<std::string> root_tags{"global"};
        REQUIRE(v.set_tags("", root_tags) == vault::VaultResult::Ok);

        // Add an image to root without its own tags.
        REQUIRE(v.add_image("", img, "pic.jpg") == vault::VaultResult::Ok);

        v.lock();
    }

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    // Search for "global" tag on images: should find the image
    // because it inherits the root's "global" tag.
    auto hits = v2.search("global", vault::SearchScope::Images);
    REQUIRE(hits.size() == 1);
    CHECK_EQ(hits[0].name, "pic.jpg");
    CHECK_TRUE(hits[0].is_gallery == false);
    // effective_tags should include "global".
    auto it = std::ranges::find(hits[0].effective_tags, "global");
    CHECK_TRUE(it != hits[0].effective_tags.end());
}

TEST(tags_cascade_from_parent_gallery)
{
    TempVault tv("cascade_parent");
    auto img = pattern(1000, 5);

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);

        // Create a gallery with tags.
        REQUIRE(v.create_gallery("vacation") == vault::VaultResult::Ok);
        std::vector<std::string> vacation_tags{"holiday"};
        REQUIRE(v.set_tags("vacation", vacation_tags) == vault::VaultResult::Ok);

        // Add an image to it with its own tags (not including "holiday").
        REQUIRE(v.add_image("vacation", img, "beach.jpg") == vault::VaultResult::Ok);
        std::vector<std::string> image_tags{"sunset"};
        REQUIRE(v.set_tags("vacation/beach.jpg", image_tags) == vault::VaultResult::Ok);

        v.lock();
    }

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    // Search for "holiday" (parent's tag) on images:
    // should find beach.jpg with effective_tags = {parent "holiday", own "sunset"}.
    auto hits = v2.search("holiday", vault::SearchScope::Images);
    REQUIRE(hits.size() == 1);
    CHECK_EQ(hits[0].name, "beach.jpg");
    CHECK_EQ(hits[0].path, "vacation/beach.jpg");
    REQUIRE(hits[0].effective_tags.size() == 2);
    auto has_holiday = std::ranges::find(hits[0].effective_tags, "holiday");
    auto has_sunset = std::ranges::find(hits[0].effective_tags, "sunset");
    CHECK_TRUE(has_holiday != hits[0].effective_tags.end());
    CHECK_TRUE(has_sunset != hits[0].effective_tags.end());
}

TEST(tags_search_scope_images_only)
{
    TempVault tv("scope_images");
    auto img = pattern(1000, 5);

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);

        // Create a gallery with "trip" tag.
        REQUIRE(v.create_gallery("pics") == vault::VaultResult::Ok);
        REQUIRE(v.set_tags("pics", {"trip"}) == vault::VaultResult::Ok);

        // Add an image with "trip" tag.
        REQUIRE(v.add_image("pics", img, "photo.jpg") == vault::VaultResult::Ok);
        REQUIRE(v.set_tags("pics/photo.jpg", {"trip"}) == vault::VaultResult::Ok);

        v.lock();
    }

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    // Search for "trip" in Images only: should find only the image.
    auto hits = v2.search("trip", vault::SearchScope::Images);
    REQUIRE(hits.size() == 1);
    CHECK_EQ(hits[0].name, "photo.jpg");
    CHECK_FALSE(hits[0].is_gallery);
}

TEST(tags_search_scope_galleries_only)
{
    TempVault tv("scope_galleries");
    auto img = pattern(1000, 5);

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);

        // Create a gallery with "trip" tag.
        REQUIRE(v.create_gallery("pics") == vault::VaultResult::Ok);
        REQUIRE(v.set_tags("pics", {"trip"}) == vault::VaultResult::Ok);

        // Add an image with "trip" tag.
        REQUIRE(v.add_image("pics", img, "photo.jpg") == vault::VaultResult::Ok);
        REQUIRE(v.set_tags("pics/photo.jpg", {"trip"}) == vault::VaultResult::Ok);

        v.lock();
    }

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    // Search for "trip" in Galleries only: should find only the gallery.
    auto hits = v2.search("trip", vault::SearchScope::Galleries);
    REQUIRE(hits.size() == 1);
    CHECK_EQ(hits[0].name, "pics");
    CHECK_TRUE(hits[0].is_gallery);
}

TEST(tags_search_scope_both)
{
    TempVault tv("scope_both");
    auto img = pattern(1000, 5);

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);

        // Create a gallery with "trip" tag.
        REQUIRE(v.create_gallery("pics") == vault::VaultResult::Ok);
        REQUIRE(v.set_tags("pics", {"trip"}) == vault::VaultResult::Ok);

        // Add an image with "trip" tag.
        REQUIRE(v.add_image("pics", img, "photo.jpg") == vault::VaultResult::Ok);
        REQUIRE(v.set_tags("pics/photo.jpg", {"trip"}) == vault::VaultResult::Ok);

        v.lock();
    }

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    // Search for "trip" in Both: should find both.
    auto hits = v2.search("trip", vault::SearchScope::Both);
    REQUIRE(hits.size() == 2);

    bool found_gallery = false, found_image = false;
    for (const auto& hit : hits) {
        if (hit.is_gallery) found_gallery = true;
        if (!hit.is_gallery) found_image = true;
    }
    CHECK_TRUE(found_gallery);
    CHECK_TRUE(found_image);
}

TEST(tags_case_insensitive_search)
{
    TempVault tv("case_insensitive");
    auto img = pattern(1000, 5);

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);

        // Add an image with a tag.
        REQUIRE(v.add_image("", img, "photo.jpg") == vault::VaultResult::Ok);
        REQUIRE(v.set_tags("photo.jpg", {"trip"}) == vault::VaultResult::Ok);

        v.lock();
    }

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    // Search with uppercase query should match lowercase tag.
    auto hits = v2.search("TRIP", vault::SearchScope::Images);
    REQUIRE(hits.size() == 1);
    CHECK_EQ(hits[0].name, "photo.jpg");
}

TEST(tags_case_insensitive_name_search)
{
    TempVault tv("case_name_search");
    auto img = pattern(1000, 5);

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);

        REQUIRE(v.add_image("", img, "BeachPhoto.jpg") == vault::VaultResult::Ok);

        v.lock();
    }

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    // Search by lowercase substring of filename.
    auto hits = v2.search("beach", vault::SearchScope::Images);
    REQUIRE(hits.size() == 1);
    CHECK_EQ(hits[0].name, "BeachPhoto.jpg");
}

TEST(tags_set_tags_nonexistent_path_returns_not_found)
{
    TempVault tv("not_found");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    // Try to set tags on a nonexistent node.
    std::vector<std::string> tags{"foo"};
    CHECK_EQ(v.set_tags("nonexistent/path", tags), vault::VaultResult::NotFound);
}

TEST(tags_add_tag_nonexistent_path_returns_not_found)
{
    TempVault tv("add_not_found");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);

    CHECK_EQ(v.add_tag("nonexistent.jpg", "tag"), vault::VaultResult::NotFound);
}

TEST(tags_operations_on_locked_vault_fail)
{
    TempVault tv("locked_ops");
    auto img = pattern(1000, 5);

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);
        REQUIRE(v.add_image("", img, "pic.jpg") == vault::VaultResult::Ok);
        v.lock();
    }

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    // Vault is now locked.

    std::vector<std::string> tags{"foo"};
    CHECK_EQ(v2.set_tags("pic.jpg", tags), vault::VaultResult::Locked);
    CHECK_EQ(v2.add_tag("pic.jpg", "bar"), vault::VaultResult::Locked);
    CHECK_EQ(v2.remove_tag("pic.jpg", "baz"), vault::VaultResult::Locked);

    // search on locked vault returns empty.
    auto hits = v2.search("foo", vault::SearchScope::Images);
    CHECK_EQ(hits.size(), 0);
}

TEST(tags_search_empty_query_matches_all)
{
    TempVault tv("empty_query");
    auto img = pattern(1000, 5);

    {
        vault::Vault v;
        REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
                == vault::VaultResult::Ok);

        REQUIRE(v.create_gallery("g1") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("g1", img, "photo.jpg") == vault::VaultResult::Ok);
        REQUIRE(v.add_image("g1", img, "photo2.jpg") == vault::VaultResult::Ok);

        v.lock();
    }

    vault::Vault v2;
    REQUIRE(vault::Vault::open(tv.str(), v2) == vault::VaultResult::Ok);
    REQUIRE(v2.unlock(bytes("pw"), {}) == vault::VaultResult::Ok);

    // Empty query matches all in-scope nodes.
    auto hits_images = v2.search("", vault::SearchScope::Images);
    REQUIRE(hits_images.size() == 2);

    auto hits_galleries = v2.search("", vault::SearchScope::Galleries);
    REQUIRE(hits_galleries.size() == 1);

    auto hits_both = v2.search("", vault::SearchScope::Both);
    REQUIRE(hits_both.size() == 3);
}

TEST(tags_add_tag_empty_or_whitespace_returns_invalid_arg)
{
    TempVault tv("empty_tag");
    auto img = pattern(1000, 5);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", img, "pic.jpg") == vault::VaultResult::Ok);

    // Try to add an empty tag.
    CHECK_EQ(v.add_tag("pic.jpg", ""), vault::VaultResult::InvalidArg);
    // Try to add a whitespace-only tag.
    CHECK_EQ(v.add_tag("pic.jpg", "   "), vault::VaultResult::InvalidArg);
}
