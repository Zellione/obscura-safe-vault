// Phase 53: naming rules for the sub-gallery a nested archive becomes.

#include "test_framework.h"
#include "ui/recursive_import.h"

#include <string>
#include <vector>

using ui::nested_gallery_name;
using ui::unique_gallery_name;

TEST(nested_gallery_name_strips_a_single_extension)
{
    CHECK_EQ(nested_gallery_name("bonus.zip"), std::string("bonus"));
    CHECK_EQ(nested_gallery_name("scans.rar"), std::string("scans"));
}

TEST(nested_gallery_name_strips_the_whole_double_extension)
{
    // "bonus.tar" would be a confusing gallery name for a .tar.gz.
    CHECK_EQ(nested_gallery_name("bonus.tar.gz"), std::string("bonus"));
}

TEST(nested_gallery_name_keeps_interior_dots)
{
    // Only the archive extension goes; a version number is part of the name.
    CHECK_EQ(nested_gallery_name("album.v2.zip"), std::string("album.v2"));
}

TEST(nested_gallery_name_preserves_spaces_and_case)
{
    CHECK_EQ(nested_gallery_name("My Holiday Photos.7z"), std::string("My Holiday Photos"));
}

TEST(nested_gallery_name_sanitizes_path_separators)
{
    // Invariant 6: an archive entry name is untrusted and becomes a path
    // component. A traversal attempt must not survive into a gallery name.
    const std::string got = nested_gallery_name("../../evil.zip");
    CHECK(got.find('/') == std::string::npos);
    CHECK(got != std::string(".."));
}

TEST(nested_gallery_name_returns_empty_when_nothing_survives)
{
    // A bare extension has no stem to name a gallery after.
    CHECK_EQ(nested_gallery_name(".zip"), std::string(""));
    CHECK_EQ(nested_gallery_name(""), std::string(""));
}

// --- collision suffixing ----------------------------------------------------

TEST(unique_gallery_name_returns_base_when_free)
{
    CHECK_EQ(unique_gallery_name("bonus", {}), std::string("bonus"));
}

TEST(unique_gallery_name_suffixes_on_collision)
{
    const std::vector<std::string> taken = {"bonus"};
    CHECK_EQ(unique_gallery_name("bonus", taken), std::string("bonus_2"));
}

TEST(unique_gallery_name_skips_past_several_collisions)
{
    // Two sibling archives both called bonus.zip, plus a real gallery already
    // named bonus_2, must all coexist.
    const std::vector<std::string> taken = {"bonus", "bonus_2", "bonus_3"};
    CHECK_EQ(unique_gallery_name("bonus", taken), std::string("bonus_4"));
}

TEST(unique_gallery_name_is_unaffected_by_unrelated_names)
{
    const std::vector<std::string> taken = {"holiday", "bonus_extra"};
    CHECK_EQ(unique_gallery_name("bonus", taken), std::string("bonus"));
}
