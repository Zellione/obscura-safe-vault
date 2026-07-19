// tests/ui/test_tag_tally.cpp
#include "test_framework.h"

#include "ui/tag_tally.h"

TEST(tag_tally_empty_input_gives_empty_tally)
{
    CHECK(ui::compute_tag_tally({}).empty());
}

TEST(tag_tally_single_node_counts_one_each)
{
    const auto tally = ui::compute_tag_tally({{"beach", "sunset"}});
    REQUIRE(tally.size() == 2);
    CHECK(tally[0].tag == "beach");
    CHECK(tally[0].count == 1);
    CHECK(tally[1].tag == "sunset");
    CHECK(tally[1].count == 1);
}

TEST(tag_tally_union_across_nodes_with_counts)
{
    const auto tally = ui::compute_tag_tally({
        {"beach", "sunset"},
        {"beach"},
        {"sunset", "vacation"},
    });
    REQUIRE(tally.size() == 3);
    // Insertion order: first-seen tag wins its position.
    CHECK(tally[0].tag == "beach");
    CHECK(tally[0].count == 2);
    CHECK(tally[1].tag == "sunset");
    CHECK(tally[1].count == 2);
    CHECK(tally[2].tag == "vacation");
    CHECK(tally[2].count == 1);
}

TEST(tag_tally_is_case_insensitive_keeping_first_seen_casing)
{
    const auto tally = ui::compute_tag_tally({{"Beach"}, {"beach"}, {"BEACH"}});
    REQUIRE(tally.size() == 1);
    CHECK(tally[0].tag == "Beach");   // first-seen casing kept
    CHECK(tally[0].count == 3);
}

TEST(tag_tally_ignores_nodes_with_no_tags)
{
    const auto tally = ui::compute_tag_tally({{}, {"solo"}, {}});
    REQUIRE(tally.size() == 1);
    CHECK(tally[0].tag == "solo");
    CHECK(tally[0].count == 1);
}
