// Phase 51: walk a picked directory into the same ZipEntry shape an archive
// reader produces, so build_zip_plan() can mirror it into sub-galleries with no
// second tree builder.

#include "test_framework.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "ui/folder_scan.h"

namespace fs = std::filesystem;

namespace {

fs::path make_tree()
{
    const fs::path root = fs::temp_directory_path() / "osv_scan_test";
    fs::remove_all(root);
    fs::create_directories(root / "sub" / "deeper");
    fs::create_directories(root / "empty");
    std::ofstream(root / "a.jpg") << "x";
    std::ofstream(root / "sub" / "b.png") << "x";
    std::ofstream(root / "sub" / "deeper" / "c.mp4") << "x";
    std::ofstream(root / "notes.txt") << "x";
    return root;
}

bool has_path(const std::vector<ui::ZipEntry>& e, std::string_view p)
{
    return std::ranges::any_of(e, [p](const ui::ZipEntry& z) { return z.path == p; });
}

} // namespace

TEST(folder_scan_emits_relative_slash_separated_paths)
{
    const auto root = make_tree();
    const auto entries = ui::scan_folder(root);
    CHECK(has_path(entries, "a.jpg"));
    CHECK(has_path(entries, "sub/b.png"));
    CHECK(has_path(entries, "sub/deeper/c.mp4"));
    fs::remove_all(root);
}

TEST(folder_scan_includes_unsupported_files_for_the_planner_to_count)
{
    // scan_folder does not filter — build_zip_plan owns the "supported media"
    // decision and counts what it skips, so the skip tally stays accurate.
    const auto root = make_tree();
    CHECK(has_path(ui::scan_folder(root), "notes.txt"));
    fs::remove_all(root);
}

TEST(folder_scan_marks_directories)
{
    const auto root = make_tree();
    const auto entries = ui::scan_folder(root);
    const auto it = std::ranges::find_if(entries,
        [](const ui::ZipEntry& z) { return z.path == "sub"; });
    CHECK(it != entries.end());
    CHECK(it->is_dir);
    fs::remove_all(root);
}

TEST(folder_scan_on_a_missing_root_is_empty)
{
    CHECK(ui::scan_folder(fs::temp_directory_path() / "osv_does_not_exist").empty());
}

TEST(folder_scan_respects_the_entry_cap)
{
    const auto root = make_tree();
    const auto entries = ui::scan_folder(root, {.max_entries = 2});
    CHECK(entries.size() <= 2u);
    fs::remove_all(root);
}
