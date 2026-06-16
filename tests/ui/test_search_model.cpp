#include "test_framework.h"

#include "ui/search_model.h"

// ==================== tokenize tests ====================

TEST(tokenize_empty_string)
{
    auto tokens = ui::tokenize("");
    CHECK(tokens.empty());
}

TEST(tokenize_whitespace_only)
{
    auto tokens = ui::tokenize("   \t  \n  ");
    CHECK(tokens.empty());
}

TEST(tokenize_single_word_lowercase)
{
    auto tokens = ui::tokenize("hello");
    REQUIRE(tokens.size() == 1);
    CHECK_EQ(tokens[0], std::string("hello"));
}

TEST(tokenize_single_word_uppercase)
{
    auto tokens = ui::tokenize("HELLO");
    REQUIRE(tokens.size() == 1);
    CHECK_EQ(tokens[0], std::string("hello"));
}

TEST(tokenize_mixed_case)
{
    auto tokens = ui::tokenize("FoO BaR");
    REQUIRE(tokens.size() == 2);
    CHECK_EQ(tokens[0], std::string("foo"));
    CHECK_EQ(tokens[1], std::string("bar"));
}

TEST(tokenize_multiple_spaces)
{
    auto tokens = ui::tokenize("foo    bar");
    REQUIRE(tokens.size() == 2);
    CHECK_EQ(tokens[0], std::string("foo"));
    CHECK_EQ(tokens[1], std::string("bar"));
}

TEST(tokenize_tabs_and_spaces)
{
    auto tokens = ui::tokenize("foo\t\tbar  baz");
    REQUIRE(tokens.size() == 3);
    CHECK_EQ(tokens[0], std::string("foo"));
    CHECK_EQ(tokens[1], std::string("bar"));
    CHECK_EQ(tokens[2], std::string("baz"));
}

TEST(tokenize_leading_trailing_whitespace)
{
    auto tokens = ui::tokenize("  hello world  ");
    REQUIRE(tokens.size() == 2);
    CHECK_EQ(tokens[0], std::string("hello"));
    CHECK_EQ(tokens[1], std::string("world"));
}

TEST(tokenize_complex_query)
{
    auto tokens = ui::tokenize("  Beautiful  SUNSET  nature  ");
    REQUIRE(tokens.size() == 3);
    CHECK_EQ(tokens[0], std::string("beautiful"));
    CHECK_EQ(tokens[1], std::string("sunset"));
    CHECK_EQ(tokens[2], std::string("nature"));
}

// ==================== matches tests ====================

TEST(matches_empty_tokens_matches_everything)
{
    std::vector<std::string> tokens;
    CHECK(ui::matches(tokens, "any name", {}));
    CHECK(ui::matches(tokens, "any name", {"tag1", "tag2"}));
}

TEST(matches_single_token_in_name)
{
    auto tokens = ui::tokenize("beach");
    CHECK(ui::matches(tokens, "beach", {}));
    CHECK(ui::matches(tokens, "My Beach House", {}));
}

TEST(matches_single_token_case_insensitive)
{
    auto tokens = ui::tokenize("BEACH");
    CHECK(ui::matches(tokens, "beach", {}));
    CHECK(ui::matches(tokens, "BeAcH Photo", {}));
}

TEST(matches_single_token_in_tags)
{
    auto tokens = ui::tokenize("sunset");
    CHECK(ui::matches(tokens, "Unknown", {"sunset", "nature"}));
    CHECK(ui::matches(tokens, "Unknown", {"ocean", "SUNSET"}));
}

TEST(matches_single_token_not_found)
{
    auto tokens = ui::tokenize("mountain");
    CHECK_FALSE(ui::matches(tokens, "beach", {}));
    CHECK_FALSE(ui::matches(tokens, "beach", {"ocean", "sunset"}));
}

TEST(matches_token_as_substring)
{
    auto tokens = ui::tokenize("sun");
    CHECK(ui::matches(tokens, "sunset", {}));
    CHECK(ui::matches(tokens, "xxx", {"sunshine"}));
}

TEST(matches_multiple_tokens_all_must_hit)
{
    auto tokens = ui::tokenize("beach sunset");
    REQUIRE(tokens.size() == 2);

    // Both tokens in name
    CHECK(ui::matches(tokens, "beach sunset photo", {}));

    // One in name, one in tags
    CHECK(ui::matches(tokens, "beach", {"sunset"}));

    // Both in tags
    CHECK(ui::matches(tokens, "image", {"beach", "sunset"}));

    // Missing one token
    CHECK_FALSE(ui::matches(tokens, "beach", {}));
    CHECK_FALSE(ui::matches(tokens, "image", {"beach"}));
}

TEST(matches_multiple_tokens_distributed_across_tags)
{
    auto tokens = ui::tokenize("nature beach");
    REQUIRE(tokens.size() == 2);

    // Token "nature" in tag1, token "beach" in tag2
    CHECK(ui::matches(tokens, "photo", {"nature", "beach"}));

    // Both tokens in the same tag (substring)
    CHECK(ui::matches(tokens, "photo", {"nature beach"}));

    // Only one token present
    CHECK_FALSE(ui::matches(tokens, "photo", {"nature"}));
}

TEST(matches_case_insensitivity_in_tags)
{
    auto tokens = ui::tokenize("sunset");
    CHECK(ui::matches(tokens, "image", {"SUNSET"}));
    CHECK(ui::matches(tokens, "image", {"SuNsEt"}));
}

// ==================== score tests ====================

TEST(score_zero_when_no_match)
{
    auto tokens = ui::tokenize("beach");
    CHECK_EQ(ui::score(tokens, "ocean", {}), 0);
    CHECK_EQ(ui::score(tokens, "ocean", {"sunset", "nature"}), 0);
}

TEST(score_zero_for_empty_tokens)
{
    std::vector<std::string> tokens;
    // Empty token list matches but shouldn't contribute to score (all matched by default)
    int s = ui::score(tokens, "any name", {});
    CHECK_EQ(s, 0);
}

TEST(score_single_token_in_name_higher_than_tag)
{
    auto tokens = ui::tokenize("beach");
    int score_name = ui::score(tokens, "beach", {});
    int score_tag = ui::score(tokens, "unknown", {"beach"});

    CHECK(score_name > 0);
    CHECK(score_tag > 0);
    CHECK(score_name > score_tag);
}

TEST(score_name_match_weighted_higher)
{
    auto tokens = ui::tokenize("beach");
    int score_name_only = ui::score(tokens, "beach", {});
    int score_both = ui::score(tokens, "beach", {"beach"});

    CHECK(score_both > score_name_only);
}

TEST(score_multiple_tokens_sum_contributions)
{
    auto tokens = ui::tokenize("beach sunset");
    int score_one = ui::score(ui::tokenize("beach"), "beach", {});
    int score_two = ui::score(tokens, "beach sunset", {});

    CHECK(score_two > score_one);
}

TEST(score_token_hits_multiple_sources)
{
    auto tokens = ui::tokenize("photo");
    int score_name_only = ui::score(tokens, "photo", {"other"});
    int score_both = ui::score(tokens, "photo", {"photo"});

    CHECK(score_both > score_name_only);
}

TEST(score_returns_higher_for_more_matches)
{
    auto tokens = ui::tokenize("beach");
    // Name hit
    int score1 = ui::score(tokens, "beach house", {});
    // Name + tag hit
    int score2 = ui::score(tokens, "beach house", {"beach"});

    CHECK(score1 > 0);
    CHECK(score2 > score1);
}

TEST(score_consistency_with_matches)
{
    auto tokens = ui::tokenize("sunset");

    // If matches returns true, score must be > 0
    if (ui::matches(tokens, "golden sunset", {})) {
        CHECK(ui::score(tokens, "golden sunset", {}) > 0);
    }

    // If matches returns false, score must be 0
    if (!ui::matches(tokens, "ocean", {})) {
        CHECK_EQ(ui::score(tokens, "ocean", {}), 0);
    }
}
