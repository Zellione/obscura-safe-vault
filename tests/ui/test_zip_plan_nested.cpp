// Phase 53: the planner routes archive entries into ZipPlan::nested instead of
// silently counting them as unsupported.
//
// Classification here is EXTENSION-ONLY (is_archive_name) — the planner has
// entry names but no entry bytes. The orchestrator magic-checks each candidate
// once it has extracted the bytes, and demotes a liar to skipped_unsupported
// there.

#include "test_framework.h"
#include "ui/zip_plan.h"
#include "ui/zip_test_helpers.h"

#include <algorithm>
#include <string>
#include <vector>

using ziptest::entries;

namespace {

[[nodiscard]] bool has_gallery(const ui::ZipPlan& p, std::string_view g)
{
    return std::ranges::find(p.galleries, std::string(g)) != p.galleries.end();
}

} // namespace

TEST(zip_plan_routes_nested_archive_out_of_skipped)
{
    auto e = entries({"a.jpg", "bonus.zip"});
    auto p = ui::build_zip_plan(e, "", "Trip");

    REQUIRE(p.nested.size() == 1);
    CHECK_EQ(p.nested[0].filename, std::string("bonus.zip"));
    // Crucially NOT counted as unsupported — that was the Phase 17 behaviour
    // this phase replaces.
    CHECK_EQ(p.skipped_unsupported, 0);
    CHECK_EQ(p.placements.size(), static_cast<size_t>(1));   // a.jpg only
}

TEST(zip_plan_nested_archive_records_its_parent_gallery)
{
    // The nested archive's sub-gallery is created under the gallery mirroring
    // the directory the archive sits in, not at the archive root.
    auto e = entries({"2020/holiday.jpg", "2020/extras/bonus.cbz"});
    auto p = ui::build_zip_plan(e, "", "Photos");

    REQUIRE(p.nested.size() == 1);
    CHECK_EQ(p.nested[0].gallery_path, std::string("Photos/2020/extras"));
    CHECK_EQ(p.nested[0].filename, std::string("bonus.cbz"));
}

TEST(zip_plan_still_counts_genuinely_unsupported_files)
{
    auto e = entries({"a.jpg", "notes.txt", "bonus.zip"});
    auto p = ui::build_zip_plan(e, "", "Trip");

    CHECK_EQ(p.skipped_unsupported, 1);                      // notes.txt
    CHECK_EQ(p.nested.size(), static_cast<size_t>(1));       // bonus.zip
}

TEST(zip_plan_nested_entry_index_points_back_at_the_archive_entry)
{
    // The orchestrator needs the index to extract the right entry's bytes.
    auto e = entries({"a.jpg", "bonus.7z"});
    auto p = ui::build_zip_plan(e, "", "Trip");

    REQUIRE(p.nested.size() == 1);
    CHECK_EQ(e[p.nested[0].entry_index].path, std::string("bonus.7z"));
}

TEST(zip_plan_creates_parent_gallery_for_an_archive_only_directory)
{
    // A directory holding nothing but a nested archive still needs its gallery
    // created, or the sub-gallery would have nowhere to attach.
    auto e = entries({"extras/bonus.zip"});
    auto p = ui::build_zip_plan(e, "", "Photos");

    REQUIRE(p.nested.size() == 1);
    CHECK(has_gallery(p, "Photos"));
    CHECK(has_gallery(p, "Photos/extras"));
}

TEST(zip_plan_routes_every_supported_archive_extension)
{
    auto e = entries({"a.zip", "b.cbz", "c.7z", "d.rar", "e.tar", "f.tar.gz", "g.tgz"});
    auto p = ui::build_zip_plan(e, "", "Box");

    CHECK_EQ(p.nested.size(), static_cast<size_t>(7));
    CHECK_EQ(p.skipped_unsupported, 0);
}

// --- CBZ deliberately does not recurse --------------------------------------

TEST(cbz_plan_does_not_recurse_into_nested_archives)
{
    // A CBZ is a flat run of pages by definition. An archive inside one is not
    // a chapter, so it stays unsupported rather than becoming a sub-gallery.
    auto e = entries({"001.jpg", "002.jpg", "bonus.zip"});
    auto p = ui::build_cbz_plan(e, "", "Chapter1");

    CHECK(p.nested.empty());
    CHECK_EQ(p.skipped_unsupported, 1);                      // bonus.zip
    CHECK_EQ(p.placements.size(), static_cast<size_t>(2));
}
