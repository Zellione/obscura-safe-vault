// Phase 55: the JSON tag-dictionary parser — pure, exception-free, and the only
// place a hostile .json file's bytes are interpreted. Every rule the design
// records (trim, colon rejection, UTF-8-boundary truncation, case-insensitive
// de-dupe, non-fatal malformed entries, caps) gets its own test here.

#include "test_framework.h"

#include "ui/tag_json_parse.h"
#include "vault/index.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

using ui::parse_tag_dict_json;
using ui::TagDictParseResult;

namespace {

TagDictParseResult parse(std::string_view s)
{
    return parse_tag_dict_json(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
}

}  // namespace

// --- shape tolerance ------------------------------------------------------

TEST(tag_json_parse_bare_array)
{
    auto r = parse(R"([{"category":"artist","name":"Kaguya","description":"Doujin artist"},
                       {"name":"landscape"}])");
    REQUIRE(r.entries.size() == 2);
    CHECK_EQ(r.entries[0].category, std::string("artist"));
    CHECK_EQ(r.entries[0].name, std::string("Kaguya"));
    CHECK_EQ(r.entries[0].description, std::string("Doujin artist"));
    CHECK(r.entries[1].category.empty());
    CHECK_EQ(r.entries[1].name, std::string("landscape"));
    CHECK(r.entries[1].description.empty());
    CHECK_EQ(r.malformed_skipped, static_cast<size_t>(0));
}

TEST(tag_json_parse_object_with_tags_key)
{
    auto r = parse(R"({"tags":[{"category":"character","name":"Alice"}]})");
    REQUIRE(r.entries.size() == 1);
    CHECK_EQ(r.entries[0].category, std::string("character"));
    CHECK_EQ(r.entries[0].name, std::string("Alice"));
}

TEST(tag_json_parse_object_without_tags_key_yields_nothing)
{
    auto r = parse(R"({"entries":[{"name":"a"}]})");
    CHECK(r.entries.empty());
    CHECK_EQ(r.malformed_skipped, static_cast<size_t>(0));
}

TEST(tag_json_parse_malformed_document_is_not_fatal)
{
    for (std::string_view bad : {"", "not json", "[", R"({"tags":)", "42", R"("string")"}) {
        auto r = parse(bad);
        CHECK(r.entries.empty());
    }
}

// --- per-entry field rules ------------------------------------------------

TEST(tag_json_parse_trims_every_field)
{
    auto r = parse(R"([{"category":"  artist \t","name":"\n Kaguya  ","description":"\t text \r\n"}])");
    REQUIRE(r.entries.size() == 1);
    CHECK_EQ(r.entries[0].category, std::string("artist"));
    CHECK_EQ(r.entries[0].name, std::string("Kaguya"));
    CHECK_EQ(r.entries[0].description, std::string("text"));
}

TEST(tag_json_parse_all_fields_empty_is_malformed)
{
    auto r = parse(R"([{"category":"","name":"","description":""}])");
    CHECK(r.entries.empty());
    CHECK_EQ(r.malformed_skipped, static_cast<size_t>(1));
}

TEST(tag_json_parse_missing_name_is_malformed)
{
    auto r = parse(R"([{"category":"artist"},{"name":"kept"},{"name":"   "}])");
    REQUIRE(r.entries.size() == 1);
    CHECK_EQ(r.entries[0].name, std::string("kept"));
    CHECK_EQ(r.malformed_skipped, static_cast<size_t>(2));
}

TEST(tag_json_parse_non_object_entry_is_malformed)
{
    auto r = parse(R"(["bare string", 7, null, {"name":"kept"}])");
    REQUIRE(r.entries.size() == 1);
    CHECK_EQ(r.entries[0].name, std::string("kept"));
    CHECK_EQ(r.malformed_skipped, static_cast<size_t>(3));
}

TEST(tag_json_parse_non_string_field_is_ignored)
{
    // A non-string category/description is treated as absent, not as a hard
    // error — the entry still carries a usable name.
    auto r = parse(R"([{"category":42,"name":"kept","description":[1,2]}])");
    REQUIRE(r.entries.size() == 1);
    CHECK_EQ(r.entries[0].name, std::string("kept"));
    CHECK(r.entries[0].category.empty());
    CHECK(r.entries[0].description.empty());
    CHECK_EQ(r.malformed_skipped, static_cast<size_t>(0));
}

TEST(tag_json_parse_rejects_colon_in_name)
{
    // Deliberate constraint: resolve_tag splits on the FIRST colon, so a colon
    // inside the name would display split at the wrong place. Never silent.
    auto r = parse(R"([{"name":"artist:Kaguya"},{"category":"artist","name":"a:b"},{"name":"ok"}])");
    REQUIRE(r.entries.size() == 1);
    CHECK_EQ(r.entries[0].name, std::string("ok"));
    CHECK_EQ(r.malformed_skipped, static_cast<size_t>(2));
}

TEST(tag_json_parse_colon_in_category_is_allowed_through_key)
{
    // A colon in the CATEGORY is harmless: the key splits at that first colon,
    // which is exactly where the category ends.
    auto r = parse(R"([{"category":"a","name":"b"}])");
    REQUIRE(r.entries.size() == 1);
    CHECK_EQ(r.entries[0].key(), std::string("a:b"));
}

TEST(tag_json_parse_key_is_bare_name_without_category)
{
    auto r = parse(R"([{"name":"landscape"}])");
    REQUIRE(r.entries.size() == 1);
    CHECK_EQ(r.entries[0].key(), std::string("landscape"));
}

// --- de-duplication -------------------------------------------------------

TEST(tag_json_parse_dedup_case_insensitive_keeps_first_casing)
{
    auto r = parse(R"([{"category":"Artist","name":"Kaguya"},
                       {"category":"artist","name":"KAGUYA","description":"dup"},
                       {"name":"solo"},{"name":"SOLO"}])");
    REQUIRE(r.entries.size() == 2);
    CHECK_EQ(r.entries[0].category, std::string("Artist"));
    CHECK_EQ(r.entries[0].name, std::string("Kaguya"));
    CHECK(r.entries[0].description.empty());   // the duplicate's text does not win
    CHECK_EQ(r.entries[1].name, std::string("solo"));
    CHECK_EQ(r.malformed_skipped, static_cast<size_t>(2));
}

TEST(tag_json_parse_same_name_under_different_categories_is_not_a_dup)
{
    auto r = parse(R"([{"category":"artist","name":"Alice"},
                       {"category":"character","name":"Alice"},
                       {"name":"Alice"}])");
    CHECK_EQ(r.entries.size(), static_cast<size_t>(3));
    CHECK_EQ(r.malformed_skipped, static_cast<size_t>(0));
}

// --- truncation -----------------------------------------------------------

TEST(tag_json_parse_truncates_long_description_and_counts_it)
{
    const std::string long_desc(vault::INDEX_MAX_TAG_DESC_BYTES + 100, 'x');
    auto r = parse(R"([{"name":"t","description":")" + long_desc + R"("}])");
    REQUIRE(r.entries.size() == 1);
    CHECK_EQ(r.entries[0].description.size(),
             static_cast<size_t>(vault::INDEX_MAX_TAG_DESC_BYTES));
    CHECK_EQ(r.fields_truncated, static_cast<size_t>(1));
    CHECK_EQ(r.malformed_skipped, static_cast<size_t>(0));   // kept, not skipped
}

TEST(tag_json_parse_truncates_description_on_utf8_boundary)
{
    // 511 ASCII bytes + a 3-byte 'い' straddles the 512-byte cap: the cut must
    // land at 511, never inside the sequence.
    std::string desc(vault::INDEX_MAX_TAG_DESC_BYTES - 1, 'a');
    desc += "\xE3\x81\x84";
    auto r = parse(R"([{"name":"t","description":")" + desc + R"("}])");
    REQUIRE(r.entries.size() == 1);
    CHECK_EQ(r.entries[0].description.size(),
             static_cast<size_t>(vault::INDEX_MAX_TAG_DESC_BYTES - 1));
    CHECK_EQ(r.entries[0].description.back(), 'a');
    CHECK_EQ(r.fields_truncated, static_cast<size_t>(1));
}

TEST(tag_json_parse_description_exactly_at_cap_is_not_truncated)
{
    const std::string desc(vault::INDEX_MAX_TAG_DESC_BYTES, 'x');
    auto r = parse(R"([{"name":"t","description":")" + desc + R"("}])");
    REQUIRE(r.entries.size() == 1);
    CHECK_EQ(r.entries[0].description.size(),
             static_cast<size_t>(vault::INDEX_MAX_TAG_DESC_BYTES));
    CHECK_EQ(r.fields_truncated, static_cast<size_t>(0));
}

TEST(tag_json_parse_truncates_long_category_on_utf8_boundary)
{
    std::string cat(vault::INDEX_MAX_CATEGORY_BYTES - 1, 'c');
    cat += "\xE3\x81\x84";
    auto r = parse(R"([{"category":")" + cat + R"(","name":"t"}])");
    REQUIRE(r.entries.size() == 1);
    CHECK_EQ(r.entries[0].category.size(),
             static_cast<size_t>(vault::INDEX_MAX_CATEGORY_BYTES - 1));
    CHECK_EQ(r.fields_truncated, static_cast<size_t>(1));
}

TEST(tag_json_parse_truncation_counts_once_per_field)
{
    const std::string cat(vault::INDEX_MAX_CATEGORY_BYTES + 10, 'c');
    const std::string desc(vault::INDEX_MAX_TAG_DESC_BYTES + 10, 'd');
    auto r = parse(R"([{"category":")" + cat + R"(","name":"t","description":")" + desc + R"("}])");
    REQUIRE(r.entries.size() == 1);
    CHECK_EQ(r.fields_truncated, static_cast<size_t>(2));
}

TEST(tag_json_parse_truncated_name_is_not_a_thing)
{
    // Names are not length-capped by the parser: the index bounds a tag key only
    // by its u16 length prefix, exactly as node tags are bounded.
    const std::string name(4000, 'n');
    auto r = parse(R"([{"name":")" + name + R"("}])");
    REQUIRE(r.entries.size() == 1);
    CHECK_EQ(r.entries[0].name.size(), static_cast<size_t>(4000));
    CHECK_EQ(r.fields_truncated, static_cast<size_t>(0));
}

// --- caps -----------------------------------------------------------------

TEST(tag_json_parse_caps_entries_at_index_max_tag_descriptions)
{
    std::string blob = "[";
    for (int i = 0; i < vault::INDEX_MAX_TAG_DESCRIPTIONS + 50; ++i) {
        if (i > 0) blob += ',';
        blob += R"({"name":"t)" + std::to_string(i) + R"("})";
    }
    blob += ']';
    auto r = parse(blob);
    CHECK_EQ(r.entries.size(), static_cast<size_t>(vault::INDEX_MAX_TAG_DESCRIPTIONS));
    CHECK_EQ(r.over_cap_skipped, static_cast<size_t>(50));
}

// --- hostile input --------------------------------------------------------

TEST(tag_json_parse_invalid_utf8_bytes_no_crash)
{
    // nlohmann rejects invalid UTF-8 in a string; with allow_exceptions=false
    // that is a discarded document, not a throw.
    const std::string blob = std::string(R"([{"name":"a)") + "\xFF\xFE" + R"("}])";
    auto r = parse(blob);
    CHECK(r.entries.empty());
}

TEST(tag_json_parse_deeply_nested_document_no_crash)
{
    std::string blob(2000, '[');
    auto r = parse(blob);
    CHECK(r.entries.empty());
}

TEST(tag_json_parse_nested_arrays_as_entries_are_malformed)
{
    auto r = parse(R"([[{"name":"buried"}],{"name":"kept"}])");
    REQUIRE(r.entries.size() == 1);
    CHECK_EQ(r.entries[0].name, std::string("kept"));
    CHECK_EQ(r.malformed_skipped, static_cast<size_t>(1));
}
