// Tests for the pure advanced-search query model (Phase 18): weighted include
// tags, exclude hard-filter, AND/OR tag groups joined at the top level,
// name-substring matching, query serialisation round-trip, and tag autocomplete.

#include "test_framework.h"

#include "ui/advanced_search_model.h"

#include <algorithm>
#include <span>
#include <string>
#include <vector>

using ui::AdvancedQuery;
using ui::Combinator;
using ui::SearchScope;
using ui::TagGroup;
using ui::WeightedTag;

namespace {

std::vector<std::string> tags(std::initializer_list<const char*> ts)
{
    return std::vector<std::string>(ts.begin(), ts.end());
}

} // namespace

TEST(adv_empty_query_matches_everything)
{
    AdvancedQuery q;
    auto r = ui::evaluate(q, "beach.jpg", tags({"summer", "sea"}));
    CHECK(r.matched);
    CHECK_EQ(r.score, 0);

    // Even a node with no tags and an empty name matches an empty query.
    auto r2 = ui::evaluate(q, "", {});
    CHECK(r2.matched);
}

TEST(adv_include_is_or_gate_and_weighted_score)
{
    AdvancedQuery q;
    q.include = {WeightedTag{"cat", 3}, WeightedTag{"dog", 1}};

    // Has one include tag → matched, score = that tag's weight.
    auto only_dog = ui::evaluate(q, "x", tags({"dog"}));
    CHECK(only_dog.matched);
    CHECK_EQ(only_dog.score, 1);

    // Has both → matched, score = sum of weights.
    auto both = ui::evaluate(q, "x", tags({"cat", "dog"}));
    CHECK(both.matched);
    CHECK_EQ(both.score, 4);

    // Has neither → not matched, score 0.
    auto neither = ui::evaluate(q, "x", tags({"bird"}));
    CHECK_FALSE(neither.matched);
    CHECK_EQ(neither.score, 0);
}

TEST(adv_exclude_is_hard_filter)
{
    AdvancedQuery q;
    q.include = {WeightedTag{"cat", 5}};
    q.exclude = {"nsfw"};

    // Excluded tag present removes the hit even though an include matches.
    auto r = ui::evaluate(q, "x", tags({"cat", "nsfw"}));
    CHECK_FALSE(r.matched);
    CHECK_EQ(r.score, 0);

    auto ok = ui::evaluate(q, "x", tags({"cat"}));
    CHECK(ok.matched);
}

TEST(adv_exclude_is_case_insensitive)
{
    AdvancedQuery q;
    q.exclude = {"NSFW"};
    auto r = ui::evaluate(q, "x", tags({"nsfw"}));
    CHECK_FALSE(r.matched);
}

TEST(adv_group_and_requires_all_tags)
{
    AdvancedQuery q;
    q.groups = {TagGroup{"g", Combinator::And, tags({"a", "b"})}};

    CHECK_FALSE(ui::evaluate(q, "x", tags({"a"})).matched);
    CHECK(ui::evaluate(q, "x", tags({"a", "b"})).matched);
}

TEST(adv_group_or_requires_any_tag)
{
    AdvancedQuery q;
    q.groups = {TagGroup{"g", Combinator::Or, tags({"a", "b"})}};

    CHECK(ui::evaluate(q, "x", tags({"a"})).matched);
    CHECK_FALSE(ui::evaluate(q, "x", tags({"c"})).matched);
}

TEST(adv_groups_top_level_join)
{
    TagGroup g1{"g1", Combinator::Or, tags({"a"})};
    TagGroup g2{"g2", Combinator::Or, tags({"b"})};

    AdvancedQuery and_q;
    and_q.groups = {g1, g2};
    and_q.group_join = Combinator::And;
    CHECK_FALSE(ui::evaluate(and_q, "x", tags({"a"})).matched);  // needs both groups
    CHECK(ui::evaluate(and_q, "x", tags({"a", "b"})).matched);

    AdvancedQuery or_q;
    or_q.groups = {g1, g2};
    or_q.group_join = Combinator::Or;
    CHECK(ui::evaluate(or_q, "x", tags({"a"})).matched);         // either group suffices
    CHECK_FALSE(ui::evaluate(or_q, "x", tags({"c"})).matched);
}

TEST(adv_name_query_is_substring_filter)
{
    AdvancedQuery q;
    q.name_query = "beach";

    CHECK(ui::evaluate(q, "Beach Day.jpg", {}).matched);   // case-insensitive
    CHECK_FALSE(ui::evaluate(q, "mountain.jpg", {}).matched);

    // Name match contributes to the score.
    CHECK(ui::evaluate(q, "beach.jpg", {}).score > 0);
}

TEST(adv_combined_clauses_are_anded)
{
    AdvancedQuery q;
    q.name_query = "img";
    q.include    = {WeightedTag{"cat", 2}};

    // Name matches but no include tag → rejected.
    CHECK_FALSE(ui::evaluate(q, "img001", tags({"dog"})).matched);
    // Both clauses satisfied → matched.
    CHECK(ui::evaluate(q, "img001", tags({"cat"})).matched);
}

TEST(adv_query_serialisation_round_trip)
{
    AdvancedQuery q;
    q.include    = {WeightedTag{"cat", 3}, WeightedTag{"dog", 1}};
    q.exclude    = {"nsfw", "blurry"};
    q.groups     = {TagGroup{"animals", Combinator::Or, tags({"cat", "dog"})},
                    TagGroup{"quality", Combinator::And, tags({"sharp"})}};
    q.group_join = Combinator::Or;
    q.name_query = "vacation";
    q.scope      = SearchScope::Galleries;

    std::vector<uint8_t> blob = ui::serialize_query(q);
    REQUIRE(!blob.empty());

    AdvancedQuery back;
    REQUIRE(ui::deserialize_query(blob, back));

    REQUIRE(back.include.size() == 2);
    CHECK_EQ(back.include[0].tag, std::string("cat"));
    CHECK_EQ(back.include[0].weight, 3);
    CHECK_EQ(back.include[1].tag, std::string("dog"));
    REQUIRE(back.exclude.size() == 2);
    CHECK_EQ(back.exclude[1], std::string("blurry"));
    REQUIRE(back.groups.size() == 2);
    CHECK_EQ(back.groups[0].name, std::string("animals"));
    CHECK(back.groups[0].combinator == Combinator::Or);
    REQUIRE(back.groups[0].tags.size() == 2);
    CHECK_EQ(back.groups[1].tags[0], std::string("sharp"));
    CHECK(back.group_join == Combinator::Or);
    CHECK_EQ(back.name_query, std::string("vacation"));
    CHECK(back.scope == SearchScope::Galleries);
}

TEST(adv_deserialize_rejects_garbage)
{
    std::vector<uint8_t> garbage = {0xFF, 0x00, 0x12, 0x34};
    AdvancedQuery out;
    CHECK_FALSE(ui::deserialize_query(garbage, out));

    AdvancedQuery out2;
    CHECK_FALSE(ui::deserialize_query(std::span<const uint8_t>{}, out2));
}

TEST(adv_tag_suggestions_prefix_ranked_and_deduped)
{
    std::vector<std::string> vocab = {"Cat", "cat", "catalog", "dog", "Caterpillar", "scat"};

    auto s = ui::tag_suggestions("cat", vocab);
    // De-duplicated case-insensitively: "Cat"/"cat" collapse to one entry.
    // Prefix matches ("cat", "catalog", "caterpillar") rank above the
    // substring-only match ("scat").
    REQUIRE(!s.empty());
    CHECK(s.front() == "Cat" || s.front() == "cat");
    // "scat" (substring, not prefix) must come after every prefix match.
    auto pos_scat = std::find(s.begin(), s.end(), std::string("scat"));
    CHECK(pos_scat == s.end() - 1);

    // No duplicate (case-insensitive) entries.
    CHECK_EQ(s.size(), static_cast<size_t>(4));  // cat, catalog, caterpillar, scat
}

TEST(adv_tag_suggestions_empty_prefix_is_empty)
{
    std::vector<std::string> vocab = {"a", "b"};
    CHECK(ui::tag_suggestions("", vocab).empty());
}

TEST(adv_move_tag_cursor_navigates)
{
    // Empty list: always -1, regardless of direction or incoming cursor.
    CHECK_EQ(ui::move_tag_cursor(-1, 1, 0), -1);
    CHECK_EQ(ui::move_tag_cursor(0, -1, 0), -1);

    // Down from "nothing selected" enters the list at row 0.
    CHECK_EQ(ui::move_tag_cursor(-1, 1, 3), 0);
    // Down advances and clamps at the last row.
    CHECK_EQ(ui::move_tag_cursor(0, 1, 3), 1);
    CHECK_EQ(ui::move_tag_cursor(2, 1, 3), 2);

    // Up decrements; row 0 returns to -1 (deselect).
    CHECK_EQ(ui::move_tag_cursor(2, -1, 3), 1);
    CHECK_EQ(ui::move_tag_cursor(0, -1, 3), -1);
    // Up from -1 stays -1.
    CHECK_EQ(ui::move_tag_cursor(-1, -1, 3), -1);

    // Out-of-range incoming cursor is clamped before moving.
    CHECK_EQ(ui::move_tag_cursor(99, 1, 3), 2);
    CHECK_EQ(ui::move_tag_cursor(99, -1, 3), 1);
}
