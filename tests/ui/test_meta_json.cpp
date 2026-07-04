#include "test_framework.h"
#include "ui/meta_json.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

// Pure parser + mapping tests for the archive `meta.json` support (Phase 27).
// The parser must be tolerant: malformed input, wrong types, and missing fields
// all degrade to empty fields — a bad meta.json never blocks an import.

namespace {

ui::ArchiveMeta parse(std::string_view s)
{
    return ui::parse_meta_json(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(s.data()), s.size()));
}

} // namespace

TEST(meta_json_parses_full_document)
{
    const auto m = parse(R"({
        "title": { "english": "English Title", "japanese": "\u65e5\u672c" },
        "tags":  [ { "type": "tag",    "name": "awesome tag" },
                   { "type": "artist", "name": "someone" } ]
    })");
    CHECK_EQ(m.title_english, std::string("English Title"));
    CHECK_EQ(m.title_japanese, std::string("\xE6\x97\xA5\xE6\x9C\xAC"));  // 日本
    REQUIRE(m.tags.size() == static_cast<size_t>(2));
    CHECK_EQ(m.tags[0], std::string("awesome tag"));   // generic "tag" type: no prefix
    CHECK_EQ(m.tags[1], std::string("artist:someone"));
}

TEST(meta_json_tolerates_missing_and_partial_fields)
{
    const auto empty = parse("{}");
    CHECK(empty.title_english.empty());
    CHECK(empty.title_japanese.empty());
    CHECK(empty.tags.empty());

    const auto jp_only = parse(R"({ "title": { "japanese": "JP" } })");
    CHECK(jp_only.title_english.empty());
    CHECK_EQ(jp_only.title_japanese, std::string("JP"));
}

TEST(meta_json_tolerates_malformed_input)
{
    CHECK(parse("").tags.empty());
    CHECK(parse("not json at all").title_english.empty());
    CHECK(parse("{ \"title\": ").title_english.empty());        // truncated
    CHECK(parse("[1, 2, 3]").title_english.empty());            // non-object root
    CHECK(parse("\xFF\xFE garbage").title_english.empty());     // invalid UTF-8
}

TEST(meta_json_ignores_unknown_keys_and_wrong_types)
{
    const auto m = parse(R"({
        "id": 42, "language": "en",
        "title": { "english": "T", "japanese": 7, "pretty": "x" },
        "tags":  [ { "type": "tag", "name": "ok" },
                   { "type": "tag" },
                   { "name": 5 },
                   17,
                   { "type": "artist", "name": "a" } ]
    })");
    CHECK_EQ(m.title_english, std::string("T"));
    CHECK(m.title_japanese.empty());                 // wrong type ignored
    REQUIRE(m.tags.size() == static_cast<size_t>(2));  // entries without a name dropped
    CHECK_EQ(m.tags[0], std::string("ok"));
    CHECK_EQ(m.tags[1], std::string("artist:a"));
}

TEST(meta_json_wrong_shape_title_and_tags)
{
    const auto m = parse(R"({ "title": "just a string", "tags": { "type": "t" } })");
    CHECK(m.title_english.empty());
    CHECK(m.title_japanese.empty());
    CHECK(m.tags.empty());
}

TEST(meta_json_tag_without_type_is_bare_name)
{
    const auto m = parse(R"({ "tags": [ { "name": "loner" }, { "type": "", "name": "n" } ] })");
    REQUIRE(m.tags.size() == static_cast<size_t>(2));
    CHECK_EQ(m.tags[0], std::string("loner"));
    CHECK_EQ(m.tags[1], std::string("n"));
}

TEST(meta_json_trims_whitespace)
{
    const auto m = parse(R"({
        "title": { "english": "  Padded  " },
        "tags":  [ { "type": " tag ", "name": " spaced name " } ]
    })");
    CHECK_EQ(m.title_english, std::string("Padded"));
    REQUIRE(m.tags.size() == static_cast<size_t>(1));
    CHECK_EQ(m.tags[0], std::string("spaced name"));   // " tag " trims to the generic type
}

TEST(meta_json_generic_tag_type_has_no_prefix)
{
    // nhentai-style metadata marks ordinary tags with the generic type "tag";
    // prefixing those ("tag:ponytail") is pure noise, so the bare name is used.
    // Real types (artist, character, parody, ...) keep their prefix.
    const auto m = parse(R"({
        "tags": [ { "type": "tag",    "name": "a" },
                  { "type": "tags",   "name": "b" },
                  { "type": "TAG",    "name": "c" },
                  { "type": "artist", "name": "d" } ]
    })");
    REQUIRE(m.tags.size() == static_cast<size_t>(4));
    CHECK_EQ(m.tags[0], std::string("a"));
    CHECK_EQ(m.tags[1], std::string("b"));
    CHECK_EQ(m.tags[2], std::string("c"));
    CHECK_EQ(m.tags[3], std::string("artist:d"));
}

TEST(meta_gallery_name_fallback_chain)
{
    ui::ArchiveMeta m;
    CHECK_EQ(ui::meta_gallery_name(m, "file"), std::string("file"));

    m.title_japanese = "JP";
    CHECK_EQ(ui::meta_gallery_name(m, "file"), std::string("JP"));

    m.title_english = "EN";
    CHECK_EQ(ui::meta_gallery_name(m, "file"), std::string("EN"));
}

TEST(meta_gallery_name_sanitises_path_separators)
{
    ui::ArchiveMeta m;
    m.title_english = "A/B/C";   // '/' would split the vault gallery path
    CHECK_EQ(ui::meta_gallery_name(m, "file"), std::string("A_B_C"));

    m.title_english = "   ";     // whitespace-only falls through
    m.title_japanese = "JP";
    CHECK_EQ(ui::meta_gallery_name(m, "file"), std::string("JP"));
}

TEST(meta_gallery_tags_include_japanese_title)
{
    ui::ArchiveMeta m;
    m.title_japanese = "JP Title";
    m.tags = {"tag:a", "artist:b"};
    const auto tags = ui::meta_gallery_tags(m);
    REQUIRE(tags.size() == static_cast<size_t>(3));
    CHECK_EQ(tags[0], std::string("JP Title"));   // stays searchable as a tag
    CHECK_EQ(tags[1], std::string("tag:a"));
    CHECK_EQ(tags[2], std::string("artist:b"));

    ui::ArchiveMeta no_jp;
    no_jp.tags = {"tag:a"};
    CHECK_EQ(ui::meta_gallery_tags(no_jp).size(), static_cast<size_t>(1));
}
