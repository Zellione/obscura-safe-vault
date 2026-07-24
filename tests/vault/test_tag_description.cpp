// Phase 51: per-tag free-text descriptions, stored in the vault-global settings
// block (v9). A description belongs to the *tag*, not to any node carrying it,
// and tags have no registry — hence vault-global rather than per-node.

#include "test_framework.h"

#include <span>
#include <string>
#include <vector>

#include "vault/index.h"

using namespace vault;

namespace {

IndexNode make_root()
{
    IndexNode root;
    root.type = IndexNode::Type::Gallery;
    root.name = "";
    return root;
}

} // namespace

TEST(tag_description_round_trips_through_serialisation)
{
    VaultSettings s;
    s.tag_descriptions = {{.tag = "beach", .text = "Coastal shots only"},
                          {.tag = "artist:Kaguya", .text = "Active 2011-2019"}};

    std::vector<uint8_t> blob;
    serialize_index(make_root(), {}, s, blob);

    IndexNode                out;
    std::vector<SavedSearch> searches;
    VaultSettings            back;
    CHECK(deserialize_index(blob, out, searches, back));
    CHECK_EQ(back.tag_descriptions.size(), 2u);
    CHECK_EQ(back.tag_descriptions[0].tag, std::string("beach"));
    CHECK_EQ(back.tag_descriptions[0].text, std::string("Coastal shots only"));
    CHECK_EQ(back.tag_descriptions[1].tag, std::string("artist:Kaguya"));
    CHECK_EQ(back.tag_descriptions[1].text, std::string("Active 2011-2019"));
}

TEST(tag_description_blob_declares_version_9)
{
    std::vector<uint8_t> blob;
    serialize_index(make_root(), {}, VaultSettings{}, blob);
    CHECK_EQ(static_cast<int>(blob[0]), 9);
}

TEST(tag_description_empty_list_round_trips)
{
    VaultSettings s;
    std::vector<uint8_t> blob;
    serialize_index(make_root(), {}, s, blob);

    IndexNode                out;
    std::vector<SavedSearch> searches;
    VaultSettings            back;
    CHECK(deserialize_index(blob, out, searches, back));
    CHECK(back.tag_descriptions.empty());
}

TEST(tag_description_over_length_text_is_rejected_not_clamped)
{
    // Hand-build a v9 blob whose description length exceeds the cap. The reader
    // must FAIL the parse rather than truncate — the Phase 37/47/49 rule.
    VaultSettings s;
    s.tag_descriptions = {{.tag = "beach", .text = "ok"}};
    std::vector<uint8_t> blob;
    serialize_index(make_root(), {}, s, blob);

    // The final entry's desc_len is the last u16 before the 2 text bytes.
    const size_t desc_len_at = blob.size() - 2 /*text*/ - 2 /*len*/;
    blob[desc_len_at]     = 0xFF;   // 65535 > INDEX_MAX_TAG_DESC_BYTES
    blob[desc_len_at + 1] = 0xFF;

    IndexNode                out;
    std::vector<SavedSearch> searches;
    VaultSettings            back;
    CHECK(!deserialize_index(blob, out, searches, back));
}

TEST(tag_description_lookup_is_case_insensitive)
{
    VaultSettings s;
    set_tag_description(s, "Beach", "Coastal shots only");
    CHECK_EQ(std::string(find_tag_description(s, "beach")), std::string("Coastal shots only"));
    CHECK_EQ(std::string(find_tag_description(s, "BEACH")), std::string("Coastal shots only"));
}

TEST(tag_description_lookup_missing_is_empty)
{
    VaultSettings s;
    CHECK(find_tag_description(s, "nope").empty());
}

TEST(tag_description_set_keeps_first_seen_casing)
{
    VaultSettings s;
    set_tag_description(s, "Beach", "one");
    set_tag_description(s, "beach", "two");
    CHECK_EQ(s.tag_descriptions.size(), 1u);
    CHECK_EQ(s.tag_descriptions[0].tag, std::string("Beach"));   // first casing wins
    CHECK_EQ(s.tag_descriptions[0].text, std::string("two"));    // text is replaced
}

TEST(tag_description_empty_text_removes_the_entry)
{
    VaultSettings s;
    set_tag_description(s, "beach", "something");
    CHECK_EQ(s.tag_descriptions.size(), 1u);
    set_tag_description(s, "beach", "");
    CHECK(s.tag_descriptions.empty());
}

TEST(tag_description_set_respects_the_entry_cap)
{
    VaultSettings s;
    for (int i = 0; i < INDEX_MAX_TAG_DESCRIPTIONS + 10; ++i)
        set_tag_description(s, "tag" + std::to_string(i), "x");
    CHECK_EQ(s.tag_descriptions.size(), static_cast<size_t>(INDEX_MAX_TAG_DESCRIPTIONS));
}
