#include "test_framework.h"

#include "ui/tag_list_parse.h"
#include "vault/index.h"

#include <span>
#include <string>
#include <vector>

using ui::parse_tag_list;

namespace {
// Wrap a string literal's bytes as a span for the parser.
std::vector<std::string> parse(std::string_view s)
{
    return parse_tag_list(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
}
}  // namespace

TEST(tag_list_parse_basic_lf)
{
    auto tags = parse("alpha\nbeta\ngamma");
    REQUIRE(tags.size() == 3);
    CHECK(tags[0] == "alpha");
    CHECK(tags[1] == "beta");
    CHECK(tags[2] == "gamma");
}

TEST(tag_list_parse_crlf)
{
    auto tags = parse("alpha\r\nbeta\r\n");
    REQUIRE(tags.size() == 2);
    CHECK(tags[0] == "alpha");
    CHECK(tags[1] == "beta");
}

TEST(tag_list_parse_drops_blank_lines)
{
    auto tags = parse("alpha\n\n   \n\t\nbeta\n");
    REQUIRE(tags.size() == 2);
    CHECK(tags[0] == "alpha");
    CHECK(tags[1] == "beta");
}

TEST(tag_list_parse_trims_surrounding_whitespace)
{
    auto tags = parse("  alpha \n\tbeta\t\n \r");
    REQUIRE(tags.size() == 2);
    CHECK(tags[0] == "alpha");
    CHECK(tags[1] == "beta");
}

TEST(tag_list_parse_dedup_case_insensitive_keeps_first_casing)
{
    auto tags = parse("Foo\nfoo\nFOO\nbar\nBAR");
    REQUIRE(tags.size() == 2);
    CHECK(tags[0] == "Foo");
    CHECK(tags[1] == "bar");
}

TEST(tag_list_parse_empty_input)
{
    CHECK(parse("").empty());
    CHECK(parse("\n\n\r\n   \n").empty());
}

TEST(tag_list_parse_no_trailing_newline)
{
    auto tags = parse("only");
    REQUIRE(tags.size() == 1);
    CHECK(tags[0] == "only");
}

TEST(tag_list_parse_caps_count_at_index_max_tags)
{
    std::string blob;
    for (int i = 0; i < vault::INDEX_MAX_TAGS + 50; ++i)
        blob += "tag" + std::to_string(i) + "\n";
    auto tags = parse(blob);
    CHECK_EQ(tags.size(), static_cast<size_t>(vault::INDEX_MAX_TAGS));
}

TEST(tag_list_parse_truncates_long_tag_to_u16_bound)
{
    std::string blob(ui::TAG_MAX_BYTES + 100, 'x');
    auto tags = parse(blob);
    REQUIRE(tags.size() == 1);
    CHECK_EQ(tags[0].size(), ui::TAG_MAX_BYTES);
}

TEST(tag_list_parse_non_utf8_bytes_no_crash)
{
    // A line containing raw high bytes is treated opaquely (no UTF-8 validation).
    const unsigned char raw[] = {'a', 0xFF, 0xFE, '\n', 'b', 0x80, '\n'};
    auto tags = parse_tag_list(std::span<const uint8_t>(raw, sizeof(raw)));
    REQUIRE(tags.size() == 2);
    CHECK_EQ(tags[0].size(), static_cast<size_t>(3));   // 'a' 0xFF 0xFE
    CHECK_EQ(tags[1].size(), static_cast<size_t>(2));   // 'b' 0x80
}
