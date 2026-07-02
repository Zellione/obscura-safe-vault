#include "test_framework.h"
#include "ui/zip_plan.h"
#include "ui/zip_test_helpers.h"

#include <algorithm>
#include <string>
#include <vector>

using ui::ZipDest; using ui::ZipConflictPolicy;
using ziptest::entries;

TEST(zip_plan_extension_classifier)
{
    CHECK(ui::is_supported_media_name("a.JPG"));
    CHECK(ui::is_supported_media_name("b.png"));
    CHECK(ui::is_supported_media_name("c.mp4"));
    CHECK(ui::is_supported_media_name("d.HEIC"));
    CHECK_FALSE(ui::is_supported_media_name("readme.txt"));
    CHECK_FALSE(ui::is_supported_media_name("noext"));
}

TEST(zip_plan_flat_zip_one_gallery)
{
    auto e = entries({"a.jpg", "b.png", "notes.txt"});
    auto p = ui::build_zip_plan(e, ZipDest::NewGallery, "", "Trip", ZipConflictPolicy::AskUser);
    CHECK_FALSE(p.needs_resolution);
    CHECK_EQ(p.skipped_unsupported, 1);              // notes.txt
    CHECK_EQ(p.placements.size(), static_cast<size_t>(2));
    CHECK_EQ(p.galleries.size(), static_cast<size_t>(1));   // "Trip"
    CHECK_EQ(p.galleries[0], std::string("Trip"));
    CHECK_EQ(p.placements[0].gallery_path, std::string("Trip"));
}

TEST(zip_plan_mirrors_nested_tree)
{
    // Clean hierarchy: every media file sits in a leaf dir; no intermediate dir
    // ("2020") directly holds media, so there is no leaf-invariant conflict.
    auto e = entries({"2020/winter/a.jpg", "2020/sub/b.jpg", "2021/c.png"});
    auto p = ui::build_zip_plan(e, ZipDest::NewGallery, "", "Photos", ZipConflictPolicy::AskUser);
    CHECK_FALSE(p.needs_resolution);
    // galleries: Photos/2020, Photos/2020/sub, Photos/2021 (+ ancestors Photos)
    CHECK(std::find(p.galleries.begin(), p.galleries.end(), std::string("Photos/2020/sub")) != p.galleries.end());
    CHECK(std::find(p.galleries.begin(), p.galleries.end(), std::string("Photos/2021")) != p.galleries.end());
    CHECK_EQ(p.placements.size(), static_cast<size_t>(3));
}

TEST(zip_plan_detects_mixed_folder)
{
    // "a" holds a.jpg AND has subdir "a/b" with media -> mixed.
    auto e = entries({"a/x.jpg", "a/b/y.jpg"});
    auto ask = ui::build_zip_plan(e, ZipDest::NewGallery, "", "G", ZipConflictPolicy::AskUser);
    CHECK(ask.needs_resolution);
    CHECK_EQ(ask.mixed_dirs.size(), static_cast<size_t>(1));

    auto flat = ui::build_zip_plan(e, ZipDest::NewGallery, "", "G", ZipConflictPolicy::FlattenMixed);
    CHECK_FALSE(flat.needs_resolution);
    // a/x.jpg redirected into a "Files" child leaf so "G/a" only parents subdirs.
    bool into_files = false;
    for (const auto& pl : flat.placements)
        if (pl.filename == "x.jpg") into_files = (pl.gallery_path == "G/a/Files");
    CHECK(into_files);

    auto skip = ui::build_zip_plan(e, ZipDest::NewGallery, "", "G", ZipConflictPolicy::SkipMixed);
    CHECK_FALSE(skip.needs_resolution);
    CHECK_EQ(skip.placements.size(), static_cast<size_t>(1));   // only a/b/y.jpg
    CHECK_EQ(skip.skipped_unsupported, 1);                      // a/x.jpg dropped
}

TEST(zip_plan_append_flattens)
{
    auto e = entries({"a/x.jpg", "a/b/y.png", "z.gif", "doc.pdf"});
    auto p = ui::build_zip_plan(e, ZipDest::Append, "Holiday/Day1", "", ZipConflictPolicy::AskUser);
    CHECK_FALSE(p.needs_resolution);
    CHECK(p.galleries.empty());
    CHECK(p.mixed_dirs.empty());
    CHECK_EQ(p.skipped_unsupported, 1);                         // doc.pdf
    CHECK_EQ(p.placements.size(), static_cast<size_t>(3));
    for (const auto& pl : p.placements) CHECK_EQ(pl.gallery_path, std::string("Holiday/Day1"));
}
