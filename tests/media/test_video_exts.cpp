#include "test_framework.h"

#include <array>
#include <string_view>

#include "ui/video_exts.h"

// ---------------------------------------------------------------------------
// Video extension whitelist tests
// ---------------------------------------------------------------------------

TEST(video_exts_contains_original_extensions)
{
    // Verify the original 5 extensions are present
    constexpr std::array originals{"mp4", "mkv", "webm", "mov", "m4v"};
    for (std::string_view ext : originals) {
        const bool found =
            std::ranges::find(ui::kVideoExts, ext) != ui::kVideoExts.end();
        CHECK(found);
    }
}

TEST(video_exts_contains_legacy_extensions)
{
    // Verify all legacy extensions are present
    constexpr std::array legacy{"avi", "mpg", "mpeg", "wmv", "asf", "flv", "ts", "m2ts", "ogv", "rm", "rmvb"};
    for (std::string_view ext : legacy) {
        const bool found =
            std::ranges::find(ui::kVideoExts, ext) != ui::kVideoExts.end();
        CHECK(found);
    }
}

TEST(video_exts_has_correct_size)
{
    // Total count: 5 original + 11 legacy = 16
    CHECK_EQ(ui::kVideoExts.size(), size_t{16});
}

TEST(video_exts_contains_no_duplicates)
{
    // Verify no extension appears twice in the array
    for (size_t i = 0; i < ui::kVideoExts.size(); ++i) {
        for (size_t j = i + 1; j < ui::kVideoExts.size(); ++j) {
            CHECK(ui::kVideoExts[i] != ui::kVideoExts[j]);
        }
    }
}
