#include "test_framework.h"
#include "ui/tag_suggest.h"
#include "vault/index.h"

#include <string>
#include <vector>

// Tests for ui::editor_tag_suggestions — the tag editor's autosuggest source
// (Phase 29). Ranking comes from the Phase 18 ui::tag_suggestions (prefix
// before substring, ci de-dupe, alphabetical ties); this layer trims the
// buffer, hides tags the node already carries, and caps the list at
// TAG_SUGGEST_MAX for the dropdown.

TEST(tag_suggest_ranks_prefix_before_substring)
{
    const std::vector<std::string> vocab{"corruption", "ponytail", "pony", "weapon:harpoon"};
    const auto s = ui::editor_tag_suggestions("pon", vocab, {});
    REQUIRE(s.size() == static_cast<size_t>(3));
    CHECK_EQ(s[0], std::string("pony"));               // prefix matches first, A→Z
    CHECK_EQ(s[1], std::string("ponytail"));
    CHECK_EQ(s[2], std::string("weapon:harpoon"));     // substring match ranks after
}

TEST(tag_suggest_excludes_tags_already_on_node)
{
    const std::vector<std::string> vocab{"ponytail", "pony", "pantyhose"};
    const auto s = ui::editor_tag_suggestions("p", vocab, {"PONYTAIL"});   // ci exclusion
    REQUIRE(s.size() == static_cast<size_t>(2));
    CHECK_EQ(s[0], std::string("pantyhose"));
    CHECK_EQ(s[1], std::string("pony"));
}

TEST(tag_suggest_empty_or_whitespace_buffer_yields_nothing)
{
    const std::vector<std::string> vocab{"ponytail"};
    CHECK(ui::editor_tag_suggestions("", vocab, {}).empty());
    CHECK(ui::editor_tag_suggestions("   ", vocab, {}).empty());
}

TEST(tag_suggest_trims_buffer_before_matching)
{
    const std::vector<std::string> vocab{"ponytail"};
    const auto s = ui::editor_tag_suggestions("  pony  ", vocab, {});
    REQUIRE(s.size() == static_cast<size_t>(1));
    CHECK_EQ(s[0], std::string("ponytail"));
}

TEST(tag_suggest_caps_at_max)
{
    std::vector<std::string> vocab;
    for (int i = 0; i < 12; ++i) vocab.push_back("tag" + std::to_string(i));
    const auto s = ui::editor_tag_suggestions("tag", vocab, {});
    CHECK_EQ(s.size(), static_cast<size_t>(ui::TAG_SUGGEST_MAX));
}

TEST(tag_suggest_dedupes_ci_keeping_first_casing)
{
    const std::vector<std::string> vocab{"Ponytail", "ponytail", "PONYTAIL"};
    const auto s = ui::editor_tag_suggestions("pony", vocab, {});
    REQUIRE(s.size() == static_cast<size_t>(1));
    CHECK_EQ(s[0], std::string("Ponytail"));
}

TEST(field_value_suggestions_scoped_and_ranked)
{
    vault::VaultSettings s;
    s.categories = {{.name = "artist", .swatch = 0, .fields = {"country"}},
                    {.name = "studio", .swatch = 1, .fields = {"country"}}};
    vault::set_tag_field_value(s, "artist:bob", "country", "Japan");
    vault::set_tag_field_value(s, "artist:ann", "country", "France");
    vault::set_tag_field_value(s, "artist:cat", "country", "japan");     // ci dupe
    vault::set_tag_field_value(s, "studio:acme", "country", "Germany");  // other category

    // Empty buffer: the full distinct pool for (artist, country), sorted ci.
    auto out = ui::field_value_suggestions("", "artist", "country", s);
    REQUIRE(out.size() == 2u);
    CHECK_EQ(out[0], std::string("France"));
    CHECK_EQ(out[1], std::string("Japan"));   // first-seen casing kept

    // Typed prefix ranks matches; other categories never leak in.
    out = ui::field_value_suggestions("ja", "artist", "country", s);
    REQUIRE(out.size() == 1u);
    CHECK_EQ(out[0], std::string("Japan"));

    out = ui::field_value_suggestions("", "artist", "missing", s);
    CHECK(out.empty());
}
