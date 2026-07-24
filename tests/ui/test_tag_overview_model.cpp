// Phase 22: pure sort/filter of the tag-overview list — by name or by count,
// with an optional case-insensitive typed-prefix filter. SDL/vault-free.

#include "test_framework.h"

#include <string>
#include <vector>

#include "ui/tag_overview_model.h"

using ui::TagTally;
using ui::TagSort;

namespace {

std::vector<TagTally> sample()
{
    // Deliberately unsorted, mixed casing, varied totals.
    return {
        {"Beach",    2, 5},   // total 7
        {"sunset",   1, 1},   // total 2
        {"Animals",  4, 0},   // total 4
        {"zebra",    0, 3},   // total 3
        {"animals2", 0, 0},   // total 0 (won't appear from the vault, but the model must cope)
    };
}

std::vector<std::string> names(const std::vector<TagTally>& v)
{
    std::vector<std::string> out;
    for (const auto& t : v) out.push_back(t.tag);
    return out;
}

} // namespace

TEST(tag_overview_total_is_galleries_plus_images)
{
    const TagTally a{"x", 2, 5};
    const TagTally b{"y", 0, 0};
    CHECK_EQ(a.total(), 7);
    CHECK_EQ(b.total(), 0);
}

TEST(tag_overview_sort_by_name_is_case_insensitive_ascending)
{
    auto v = sample();
    ui::sort_tags(v, TagSort::Name);
    // animals, animals2, beach, sunset, zebra (case-insensitive).
    CHECK_EQ(names(v), (std::vector<std::string>{"Animals", "animals2", "Beach", "sunset", "zebra"}));
}

TEST(tag_overview_sort_by_count_is_descending_total_then_name)
{
    auto v = sample();
    ui::sort_tags(v, TagSort::Count);
    // Totals: Beach 7, Animals 4, zebra 3, sunset 2, animals2 0.
    CHECK_EQ(names(v), (std::vector<std::string>{"Beach", "Animals", "zebra", "sunset", "animals2"}));
}

TEST(tag_overview_sort_by_count_breaks_ties_by_name)
{
    std::vector<TagTally> v{
        {"delta", 1, 1},   // total 2
        {"alpha", 0, 2},   // total 2
        {"Charlie", 2, 0}, // total 2
    };
    ui::sort_tags(v, TagSort::Count);
    // Equal totals → case-insensitive name ascending: alpha, Charlie, delta.
    CHECK_EQ(names(v), (std::vector<std::string>{"alpha", "Charlie", "delta"}));
}

TEST(tag_overview_filter_empty_prefix_returns_all)
{
    auto v = sample();
    auto f = ui::filter_tags(v, "");
    CHECK_EQ(f.size(), v.size());
}

TEST(tag_overview_filter_prefix_is_case_insensitive)
{
    auto v = sample();
    auto f = ui::filter_tags(v, "AN");
    // "Animals" and "animals2" start with "an" (case-insensitive); order preserved.
    CHECK_EQ(names(f), (std::vector<std::string>{"Animals", "animals2"}));
}

TEST(tag_overview_filter_matches_prefix_not_substring)
{
    auto v = sample();
    // "set" is a substring of "sunset" but not a prefix → no match.
    CHECK_TRUE(ui::filter_tags(v, "set").empty());
    // "sun" is a prefix of "sunset".
    CHECK_EQ(ui::filter_tags(v, "sun").size(), static_cast<size_t>(1));
}

TEST(tag_overview_filter_preserves_input_order)
{
    std::vector<TagTally> v{{"bravo", 1, 0}, {"banana", 0, 1}, {"bongo", 0, 1}};
    auto f = ui::filter_tags(v, "b");
    CHECK_EQ(names(f), (std::vector<std::string>{"bravo", "banana", "bongo"}));
}

TEST(tag_overview_sort_preserves_descriptions)
{
    std::vector<ui::TagTally> t{
        {.tag = "zebra", .gallery_count = 1, .image_count = 0, .description = "striped"},
        {.tag = "apple", .gallery_count = 1, .image_count = 0, .description = "fruit"}};
    ui::sort_tags(t, ui::TagSort::Name);
    CHECK_EQ(t[0].tag, std::string("apple"));
    CHECK_EQ(t[0].description, std::string("fruit"));
    CHECK_EQ(t[1].description, std::string("striped"));
}

TEST(tag_overview_filter_preserves_descriptions)
{
    std::vector<ui::TagTally> t{
        {.tag = "apple", .gallery_count = 1, .image_count = 0, .description = "fruit"}};
    const auto out = ui::filter_tags(t, "ap");
    CHECK_EQ(out.size(), 1u);
    CHECK_EQ(out[0].description, std::string("fruit"));
}

TEST(tag_overview_filter_matches_names_not_descriptions)
{
    // Descriptions are displayed, never searched — the Phase 51 spec keeps the
    // existing name-only filter semantics deliberately.
    std::vector<ui::TagTally> t{
        {.tag = "apple", .gallery_count = 1, .image_count = 0, .description = "zebra"}};
    CHECK(ui::filter_tags(t, "zebra").empty());
}

TEST(tag_overview_page_size_floors_to_whole_rows)
{
    CHECK_EQ(ui::tag_overview_page_size(400.0f, 48.0f), 8);   // 8.33 -> 8
    CHECK_EQ(ui::tag_overview_page_size(96.0f,  48.0f), 2);
}

TEST(tag_overview_page_size_is_at_least_one_row)
{
    // A viewport too short for even one row must still page by 1, or navigation
    // divides by zero and the screen renders nothing forever.
    CHECK_EQ(ui::tag_overview_page_size(10.0f, 48.0f), 1);
    CHECK_EQ(ui::tag_overview_page_size(0.0f,  48.0f), 1);
}
