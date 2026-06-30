#include "test_framework.h"
#include "ui/zip_plan.h"

#include <initializer_list>
#include <string>
#include <vector>

using ui::ZipEntry;

static std::vector<ZipEntry> entries(std::initializer_list<const char*> names)
{
    std::vector<ZipEntry> v;
    for (const char* n : names) {
        std::string s = n;
        v.push_back({s, !s.empty() && s.back() == '/'});
    }
    return v;
}

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

    CHECK_FALSE(p.needs_resolution);
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
