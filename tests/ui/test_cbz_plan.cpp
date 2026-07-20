#include "test_framework.h"
#include "ui/zip_plan.h"
#include "ui/zip_test_helpers.h"
#include "vault/safe_name.h"

#include <string>

using ziptest::entries;

TEST(cbz_plan_image_classifier)
{
    CHECK(ui::is_supported_image_name("page.JPG"));
    CHECK(ui::is_supported_image_name("p.png"));
    CHECK(ui::is_supported_image_name("p.webp"));
    // Video containers are NOT images for the CBZ path.
    CHECK_FALSE(ui::is_supported_image_name("clip.mp4"));
    CHECK_FALSE(ui::is_supported_image_name("clip.mkv"));
    CHECK_FALSE(ui::is_supported_image_name("readme.txt"));
    CHECK_FALSE(ui::is_supported_image_name("noext"));
}

TEST(cbz_plan_one_leaf_gallery_natural_order)
{
    // Pages out of order + an unsupported entry; subfolders to flatten.
    auto e = entries({"10.jpg", "2.jpg", "1.jpg", "cover.png", "notes.txt"});
    auto p = ui::build_cbz_plan(e, "", "MyComic");

    CHECK_EQ(p.skipped_unsupported, 1);                          // notes.txt
    CHECK_EQ(p.galleries.size(), static_cast<size_t>(1));
    CHECK_EQ(p.galleries[0], std::string("MyComic"));
    CHECK_EQ(p.placements.size(), static_cast<size_t>(4));
    for (const auto& pl : p.placements)
        CHECK_EQ(pl.gallery_path, std::string("MyComic"));
    // Natural reading order: 1, 2, 10, then cover ('c' > digits).
    CHECK_EQ(p.placements[0].filename, std::string("1.jpg"));
    CHECK_EQ(p.placements[1].filename, std::string("2.jpg"));
    CHECK_EQ(p.placements[2].filename, std::string("10.jpg"));
    CHECK_EQ(p.placements[3].filename, std::string("cover.png"));
}

TEST(cbz_plan_flattens_subfolders_by_full_path)
{
    // Two chapter folders; pages flatten into one gallery, ordered by full path.
    auto e = entries({"ch2/01.jpg", "ch1/02.jpg", "ch1/01.jpg", "ch2/", "ch1/"});
    auto p = ui::build_cbz_plan(e, "Library", "Vol1");

    CHECK_EQ(p.galleries.size(), static_cast<size_t>(1));
    CHECK_EQ(p.galleries[0], std::string("Library/Vol1"));       // base + name
    CHECK_EQ(p.placements.size(), static_cast<size_t>(3));       // dir entries skipped silently
    CHECK_EQ(p.skipped_unsupported, 0);                          // dirs aren't "unsupported files"
    // ch1/01 < ch1/02 < ch2/01 by natural full-path order. The flattened ch2 page
    // collides with ch1's "01.jpg", so it is disambiguated by its source dir.
    CHECK_EQ(p.placements[0].filename, std::string("01.jpg"));
    CHECK_EQ(p.placements[1].filename, std::string("02.jpg"));
    CHECK_EQ(p.placements[2].filename, std::string("ch2_01.jpg"));
    for (const auto& pl : p.placements)
        CHECK_EQ(pl.gallery_path, std::string("Library/Vol1"));
}

TEST(cbz_plan_empty_archive_makes_no_gallery)
{
    auto e = entries({"readme.txt", "movie.mp4"});
    auto p = ui::build_cbz_plan(e, "", "Empty");
    CHECK(p.placements.empty());
    CHECK(p.galleries.empty());                                  // nothing to import → no gallery
    CHECK_EQ(p.skipped_unsupported, 2);
}

TEST(cbz_plan_excludes_meta_json_silently)
{
    // meta.json is consumed by the importer (Phase 27) — neither a page nor
    // counted as a skipped file.
    auto e = entries({"1.jpg", "meta.json", "notes.txt"});
    auto p = ui::build_cbz_plan(e, "", "C");
    CHECK_EQ(p.placements.size(), static_cast<size_t>(1));
    CHECK_EQ(p.skipped_unsupported, 1);                          // notes.txt only
}

// --- Archive entry names are attacker-controlled ---------------------------
//
// A crafted archive can name an entry "../../.bashrc" or "/etc/cron.d/x". The
// planner must defang it, so the name that reaches Vault::add_image (and, later,
// export) can only ever be a single inert filename. Vault::add_image rejects
// anything else outright, so an un-defanged name would also silently lose pages.

TEST(cbz_plan_defangs_traversal_entry_names)
{
    auto e = entries({"../../evil.jpg", "sub/../../../etc/passwd.png", "ok.jpg"});
    auto p = ui::build_cbz_plan(e, "", "Comic");

    REQUIRE(p.placements.size() == 3);
    for (const auto& pl : p.placements) {
        CHECK_TRUE(vault::is_safe_node_name(pl.filename));
        CHECK_TRUE(pl.filename.find('/') == std::string::npos);
        CHECK_TRUE(pl.filename.find('\\') == std::string::npos);
    }
}

TEST(zip_plan_defangs_traversal_entry_and_directory_names)
{
    auto e = entries({"../evil.jpg", "..\\sneaky\\x.png", "good/a.jpg"});
    auto p = ui::build_zip_plan(e, "", "Imported");

    for (const auto& pl : p.placements) {
        CHECK_TRUE(vault::is_safe_node_name(pl.filename));
        // Every gallery path segment must be a safe node name too: a ".." segment
        // would be rejected by Vault::create_gallery and lose the whole subtree.
        for (const auto& g : p.galleries) {
            size_t start = 0;
            while (start <= g.size()) {
                const size_t slash = g.find('/', start);
                const std::string seg =
                    g.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                if (!seg.empty()) CHECK_TRUE(vault::is_safe_node_name(seg));
                if (slash == std::string::npos) break;
                start = slash + 1;
            }
        }
    }
}
