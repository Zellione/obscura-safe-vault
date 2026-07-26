// Phase 55: applying a parsed tag dictionary to a vault's settings block.
//
// apply_tag_dict mutates a VaultSettings struct and nothing else — no vault, no
// disk, no SDL — so almost everything here is a plain in-memory assertion. The
// one test that does open a vault checks the whole round trip: import, commit,
// lock, reopen, settings intact.

#include "test_framework.h"

#include "ui/tag_dict_import.h"
#include "ui/tag_json_parse.h"
#include "ui/tag_overview_model.h"
#include "vault/index.h"
#include "vault/vault.h"
#include "vault/vault_search.h"

#include <algorithm>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using ui::apply_tag_dict;
using ui::TagDictEntry;
using ui::TagDictParseResult;
using vault::VaultSettings;

namespace {

// One entry, all three fields spelled out (the build is -Werror on a partially
// designated aggregate). An empty category means a bare tag.
TagDictEntry e(std::string category, std::string name, std::string description = "")
{
    return {.category     = std::move(category),
            .name         = std::move(name),
            .description  = std::move(description)};
}

TagDictParseResult from(std::vector<TagDictEntry> entries)
{
    return TagDictParseResult{.entries = std::move(entries)};
}

// The swatch a category was registered with, or -1 when it is not registered.
int swatch_of(const VaultSettings& s, std::string_view name)
{
    for (const auto& c : s.categories) {
        if (c.name == name) return static_cast<int>(c.swatch);
    }
    return -1;
}

}  // namespace

// --- categories -----------------------------------------------------------

TEST(tag_dict_import_registers_new_category_with_auto_swatch)
{
    VaultSettings s;   // no categories at all
    const auto sum = apply_tag_dict(s, from({e("artist", "Kaguya")}));

    CHECK_EQ(sum.categories_added, static_cast<size_t>(1));
    REQUIRE(s.categories.size() == 1);
    CHECK_EQ(s.categories[0].name, std::string("artist"));
    CHECK_EQ(static_cast<int>(s.categories[0].swatch), 0);
}

TEST(tag_dict_import_existing_category_keeps_its_colour)
{
    VaultSettings s;
    s.categories = {{"artist", 9}};
    const auto sum = apply_tag_dict(s, from({e("ARTIST", "Kaguya")}));

    CHECK_EQ(sum.categories_added, static_cast<size_t>(0));
    REQUIRE(s.categories.size() == 1);
    CHECK_EQ(s.categories[0].name, std::string("artist"));   // first-seen casing kept
    CHECK_EQ(static_cast<int>(s.categories[0].swatch), 9);   // never recoloured
}

TEST(tag_dict_import_new_categories_take_the_lowest_free_swatch)
{
    VaultSettings s;
    s.categories = {{"a", 0}, {"b", 2}};   // 1 is free
    const auto sum = apply_tag_dict(s, from({e("c", "x"),
                                             e("d", "y")}));

    CHECK_EQ(sum.categories_added, static_cast<size_t>(2));
    CHECK_EQ(swatch_of(s, "c"), 1);
    CHECK_EQ(swatch_of(s, "d"), 3);
}

TEST(tag_dict_import_swatch_wraps_round_robin_once_all_sixteen_are_used)
{
    VaultSettings s;
    for (int i = 0; i < vault::TAG_SWATCH_COUNT; ++i)
        s.categories.push_back({"c" + std::to_string(i), static_cast<uint8_t>(i)});

    const auto sum = apply_tag_dict(s, from({e("extra", "x")}));
    CHECK_EQ(sum.categories_added, static_cast<size_t>(1));
    // 16 categories already present → 16 % 16 = 0. A duplicate swatch is the
    // recorded tradeoff; failing the import instead would be worse.
    CHECK_EQ(swatch_of(s, "extra"), 0);
}

TEST(tag_dict_import_registers_each_new_category_once)
{
    VaultSettings s;
    const auto sum = apply_tag_dict(s, from({e("artist", "a"),
                                             e("Artist", "b"),
                                             e("artist", "c")}));
    CHECK_EQ(sum.categories_added, static_cast<size_t>(1));
    CHECK_EQ(s.categories.size(), static_cast<size_t>(1));
}

TEST(tag_dict_import_bare_entry_registers_no_category)
{
    VaultSettings s;
    const auto sum = apply_tag_dict(s, from({e("", "landscape")}));
    CHECK_EQ(sum.categories_added, static_cast<size_t>(0));
    CHECK(s.categories.empty());
}

TEST(tag_dict_import_category_cap_is_reported_not_silent)
{
    VaultSettings s;
    for (int i = 0; i < vault::INDEX_MAX_TAG_CATEGORIES; ++i)
        s.categories.push_back({"c" + std::to_string(i), 0});

    const auto sum = apply_tag_dict(s, from({e("overflow", "x", "kept anyway")}));
    CHECK_EQ(sum.categories_added, static_cast<size_t>(0));
    CHECK_EQ(sum.categories_skipped_over_cap, static_cast<size_t>(1));
    CHECK_EQ(s.categories.size(), static_cast<size_t>(vault::INDEX_MAX_TAG_CATEGORIES));
    // The description still lands: only the colour registration was refused.
    CHECK_EQ(sum.descriptions_added, static_cast<size_t>(1));
    CHECK_EQ(vault::find_tag_description(s, "overflow:x"), std::string_view("kept anyway"));
}

// --- descriptions ---------------------------------------------------------

TEST(tag_dict_import_adds_description_under_the_full_tag_key)
{
    VaultSettings s;
    const auto sum = apply_tag_dict(s, from({e("artist", "Kaguya", "active 2011-2019")}));
    CHECK_EQ(sum.descriptions_added, static_cast<size_t>(1));
    CHECK_EQ(sum.descriptions_updated, static_cast<size_t>(0));
    CHECK_EQ(vault::find_tag_description(s, "artist:Kaguya"),
             std::string_view("active 2011-2019"));
}

TEST(tag_dict_import_adds_description_under_a_bare_key)
{
    VaultSettings s;
    const auto sum = apply_tag_dict(s, from({e("", "landscape", "outdoors")}));
    CHECK_EQ(sum.descriptions_added, static_cast<size_t>(1));
    CHECK_EQ(vault::find_tag_description(s, "landscape"), std::string_view("outdoors"));
}

TEST(tag_dict_import_updates_an_existing_description)
{
    VaultSettings s;
    vault::set_tag_description(s, "artist:Kaguya", "old");
    const auto sum = apply_tag_dict(s, from({e("artist", "Kaguya", "new")}));
    CHECK_EQ(sum.descriptions_added, static_cast<size_t>(0));
    CHECK_EQ(sum.descriptions_updated, static_cast<size_t>(1));
    CHECK_EQ(vault::find_tag_description(s, "artist:Kaguya"), std::string_view("new"));
}

TEST(tag_dict_import_identical_description_is_neither_added_nor_updated)
{
    VaultSettings s;
    vault::set_tag_description(s, "landscape", "outdoors");
    const auto sum = apply_tag_dict(s, from({e("", "landscape", "outdoors")}));
    CHECK_EQ(sum.descriptions_added, static_cast<size_t>(0));
    CHECK_EQ(sum.descriptions_updated, static_cast<size_t>(0));
}

TEST(tag_dict_import_empty_description_preserves_an_existing_one)
{
    // The deliberate Phase 51 divergence: editing a description to empty in the
    // F2 overlay REMOVES it (an intentional act); an import with an empty field
    // is ambiguous, so it leaves what was there alone.
    VaultSettings s;
    vault::set_tag_description(s, "landscape", "outdoors");
    const auto sum = apply_tag_dict(s, from({e("", "landscape")}));

    CHECK_EQ(sum.descriptions_added, static_cast<size_t>(0));
    CHECK_EQ(sum.descriptions_updated, static_cast<size_t>(0));
    CHECK_EQ(vault::find_tag_description(s, "landscape"), std::string_view("outdoors"));
}

TEST(tag_dict_import_empty_description_stores_nothing_for_a_new_tag)
{
    VaultSettings s;
    const auto sum = apply_tag_dict(s, from({e("artist", "Kaguya")}));
    CHECK_EQ(sum.descriptions_added, static_cast<size_t>(0));
    CHECK(s.tag_descriptions.empty());
    CHECK_EQ(sum.categories_added, static_cast<size_t>(1));   // the category still registers
}

TEST(tag_dict_import_description_matching_is_case_insensitive)
{
    VaultSettings s;
    vault::set_tag_description(s, "Artist:Kaguya", "old");
    const auto sum = apply_tag_dict(s, from({e("artist", "KAGUYA", "new")}));
    CHECK_EQ(sum.descriptions_updated, static_cast<size_t>(1));
    REQUIRE(s.tag_descriptions.size() == 1);
    CHECK_EQ(s.tag_descriptions[0].tag, std::string("Artist:Kaguya"));  // first casing kept
    CHECK_EQ(s.tag_descriptions[0].text, std::string("new"));
}

TEST(tag_dict_import_description_cap_is_reported_not_silent)
{
    VaultSettings s;
    for (int i = 0; i < vault::INDEX_MAX_TAG_DESCRIPTIONS; ++i)
        vault::set_tag_description(s, "t" + std::to_string(i), "x");

    const auto sum = apply_tag_dict(s, from({e("", "overflow", "no room"),
                                             e("", "t0", "updated")}));
    CHECK_EQ(sum.descriptions_added, static_cast<size_t>(0));
    CHECK_EQ(sum.entries_skipped_over_cap, static_cast<size_t>(1));
    // An EXISTING key is still updatable at the cap — no new slot is needed.
    CHECK_EQ(sum.descriptions_updated, static_cast<size_t>(1));
    CHECK_EQ(vault::find_tag_description(s, "t0"), std::string_view("updated"));
}

// --- parse counts carried through ----------------------------------------

TEST(tag_dict_import_carries_the_parse_counts_into_the_summary)
{
    VaultSettings s;
    TagDictParseResult parsed = from({e("", "a", "d")});
    parsed.malformed_skipped = 3;
    parsed.fields_truncated  = 2;
    parsed.over_cap_skipped  = 7;

    const auto sum = apply_tag_dict(s, parsed);
    CHECK_EQ(sum.entries_skipped_malformed, static_cast<size_t>(3));
    CHECK_EQ(sum.fields_truncated, static_cast<size_t>(2));
    // Entries the FILE overflowed and entries the VAULT had no room for are the
    // same thing to the user: both are "skipped, over a cap".
    CHECK_EQ(sum.entries_skipped_over_cap, static_cast<size_t>(7));
}

TEST(tag_dict_import_of_nothing_changes_nothing)
{
    VaultSettings s = VaultSettings::seeded();
    const VaultSettings before = s;
    const auto sum = apply_tag_dict(s, from({}));

    CHECK(s.categories == before.categories);
    CHECK(s.tag_descriptions.empty());
    CHECK_EQ(sum.categories_added, static_cast<size_t>(0));
    CHECK_EQ(sum.descriptions_added, static_cast<size_t>(0));
}

// --- summary wording ------------------------------------------------------

namespace {

bool any_line_contains(const std::vector<std::string>& lines, std::string_view needle)
{
    return std::ranges::any_of(lines, [needle](const std::string& l) { return l.contains(needle); });
}

}  // namespace

TEST(tag_dict_summary_always_reports_what_changed)
{
    const auto lines = ui::tag_dict_summary_lines({});
    REQUIRE(lines.size() == 3);
    CHECK(any_line_contains(lines, "Categories added:     0"));
    CHECK(any_line_contains(lines, "Descriptions added:   0"));
    CHECK(any_line_contains(lines, "Descriptions updated: 0"));
}

TEST(tag_dict_summary_counts_match_the_applied_counts)
{
    VaultSettings s;
    const auto sum = apply_tag_dict(s, from({e("artist", "Kaguya", "a"),
                                             e("artist", "Alice", "b"),
                                             e("", "landscape", "c")}));
    const auto lines = ui::tag_dict_summary_lines(sum);

    CHECK_EQ(sum.categories_added, static_cast<size_t>(1));
    CHECK_EQ(sum.descriptions_added, static_cast<size_t>(3));
    CHECK(any_line_contains(lines, "Categories added:     1"));
    CHECK(any_line_contains(lines, "Descriptions added:   3"));
}

TEST(tag_dict_summary_mentions_every_non_zero_skip)
{
    const auto lines = ui::tag_dict_summary_lines({.entries_skipped_malformed    = 2,
                                                   .fields_truncated             = 3,
                                                   .entries_skipped_over_cap     = 4,
                                                   .categories_skipped_over_cap  = 5});
    CHECK(any_line_contains(lines, "malformed): 2"));
    CHECK(any_line_contains(lines, "shortened to fit: 3"));
    CHECK(any_line_contains(lines, "Entries skipped (vault full): 4"));
    CHECK(any_line_contains(lines, "Categories not registered (vault full): 5"));
}

TEST(tag_dict_summary_stays_short_when_nothing_was_skipped)
{
    const auto lines = ui::tag_dict_summary_lines({.categories_added = 1, .descriptions_added = 2});
    CHECK_EQ(lines.size(), static_cast<size_t>(3));
    CHECK_FALSE(any_line_contains(lines, "skipped"));
    CHECK_FALSE(any_line_contains(lines, "shortened"));
}

// --- end-to-end over a real vault ----------------------------------------

namespace {

// Internal linkage: several test files define their own TempVault with a
// different layout; at namespace scope those would be ODR violations.
struct TempVault {
    fs::path     path;
    vault::Vault v;

    TempVault()
    {
        static int ctr = 0;
        path = fs::temp_directory_path() / ("osv_tagdict_test_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }

    ~TempVault()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }

    TempVault(const TempVault&)            = delete;
    TempVault& operator=(const TempVault&) = delete;
};

}  // namespace

TEST(tag_dict_import_survives_lock_and_reopen)
{
    static const crypto::KdfParams kdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};
    const std::string pw = "testpw";
    const std::span<const uint8_t> pw_bytes{reinterpret_cast<const uint8_t*>(pw.data()), pw.size()};

    TempVault tv;
    REQUIRE(vault::Vault::create(tv.path.string(), pw_bytes, {}, kdf, tv.v) ==
            vault::VaultResult::Ok);

    auto s = vault::vault_settings(tv.v);
    const auto sum = apply_tag_dict(s, from({e("artist", "Kaguya", "Doujin artist"),
                                             e("", "landscape", "outdoors")}));
    CHECK_EQ(sum.descriptions_added, static_cast<size_t>(2));
    REQUIRE(vault::set_vault_settings(tv.v, std::move(s)) == vault::VaultResult::Ok);

    tv.v.lock();
    REQUIRE(vault::Vault::open(tv.path.string(), tv.v) == vault::VaultResult::Ok);
    REQUIRE(tv.v.unlock(pw_bytes, {}) == vault::VaultResult::Ok);

    const auto& reopened = vault::vault_settings(tv.v);
    CHECK_EQ(vault::find_tag_description(reopened, "artist:Kaguya"),
             std::string_view("Doujin artist"));
    CHECK_EQ(vault::find_tag_description(reopened, "landscape"), std::string_view("outdoors"));
    CHECK_EQ(swatch_of(reopened, "artist"), 0);
}

// What the tag overview screen actually shows after an import: a description
// lands on the row of a tag the vault ALREADY carries. A tag nothing carries yet
// is stored in the settings block but has no row to show — the overview lists
// tags in use, and this phase deliberately tags nothing.
TEST(tag_dict_import_description_shows_on_the_overview_for_a_tag_in_use)
{
    static const crypto::KdfParams kdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};
    const std::string pw = "testpw";
    const std::span<const uint8_t> pw_bytes{reinterpret_cast<const uint8_t*>(pw.data()), pw.size()};

    TempVault tv;
    REQUIRE(vault::Vault::create(tv.path.string(), pw_bytes, {}, kdf, tv.v) ==
            vault::VaultResult::Ok);
    REQUIRE(tv.v.create_gallery("trip") == vault::VaultResult::Ok);
    REQUIRE(tv.v.set_tags("trip", {"artist:Kaguya"}) == vault::VaultResult::Ok);

    auto settings = vault::vault_settings(tv.v);
    const auto sum = apply_tag_dict(settings, from({e("artist", "Kaguya", "Doujin artist"),
                                                    e("", "unused", "nothing carries this")}));
    CHECK_EQ(sum.descriptions_added, static_cast<size_t>(2));
    REQUIRE(vault::set_vault_settings(tv.v, std::move(settings)) == vault::VaultResult::Ok);

    const auto rows = vault::VaultSearch(tv.v).tag_overview();
    const auto it = std::ranges::find_if(
        rows, [](const ui::TagTally& t) { return t.tag == "artist:Kaguya"; });
    REQUIRE(it != rows.end());
    CHECK_EQ(it->description, std::string("Doujin artist"));

    // Stored, but no row: the vocabulary is ahead of the content, by design.
    CHECK(std::ranges::none_of(rows, [](const ui::TagTally& t) { return t.tag == "unused"; }));
    CHECK_EQ(vault::find_tag_description(vault::vault_settings(tv.v), "unused"),
             std::string_view("nothing carries this"));
}

// --- re-importing an already-imported dictionary --------------------------
//
// The individual rules above each pin one half of this (an existing category
// keeps its colour; an identical description is neither added nor updated), but
// the property owners actually rely on is the whole-file one: importing the same
// dictionary twice must be a complete no-op, not merely "mostly harmless".

TEST(tag_dict_import_reimporting_the_same_file_is_a_no_op)
{
    VaultSettings s;
    const auto parsed = from({e("artist", "Kaguya", "Doujin artist"),
                              e("character", "Alice", "Recurring protagonist"),
                              e("", "landscape", "outdoors"),
                              e("group", "Circle")});   // category, no description

    const auto first = apply_tag_dict(s, parsed);
    CHECK_EQ(first.categories_added, static_cast<size_t>(3));
    CHECK_EQ(first.descriptions_added, static_cast<size_t>(3));

    const VaultSettings after_first = s;
    const auto          second      = apply_tag_dict(s, parsed);

    // Every counter zero — nothing added, nothing updated, nothing skipped.
    CHECK_EQ(second.categories_added, static_cast<size_t>(0));
    CHECK_EQ(second.descriptions_added, static_cast<size_t>(0));
    CHECK_EQ(second.descriptions_updated, static_cast<size_t>(0));
    CHECK_EQ(second.entries_skipped_malformed, static_cast<size_t>(0));
    CHECK_EQ(second.entries_skipped_over_cap, static_cast<size_t>(0));
    CHECK_EQ(second.categories_skipped_over_cap, static_cast<size_t>(0));
    CHECK_EQ(second.fields_truncated, static_cast<size_t>(0));

    // ...and the settings are unchanged, down to order and casing: no duplicate
    // category row, no duplicate description key, no re-assigned swatch.
    CHECK(s.categories == after_first.categories);
    CHECK(s.tag_descriptions == after_first.tag_descriptions);
    CHECK(s.default_sort == after_first.default_sort);
    CHECK_EQ(s.tiles_show_tags, after_first.tiles_show_tags);

    // The summary a second import shows says so plainly rather than staying blank.
    const auto lines = ui::tag_dict_summary_lines(second);
    CHECK(any_line_contains(lines, "Categories added:     0"));
    CHECK(any_line_contains(lines, "Descriptions added:   0"));
    CHECK(any_line_contains(lines, "Descriptions updated: 0"));
}

// The no-op above must not be "a second apply never does anything": a revised
// dictionary still lands, and touches only the entries that actually changed.
TEST(tag_dict_import_reimport_updates_only_what_changed)
{
    VaultSettings s;
    const auto first = apply_tag_dict(s, from({e("artist", "Kaguya", "old"),
                                               e("", "landscape", "outdoors")}));
    CHECK_EQ(first.descriptions_added, static_cast<size_t>(2));

    const auto second = apply_tag_dict(s, from({e("artist", "Kaguya", "revised"),
                                                e("", "landscape", "outdoors")}));
    CHECK_EQ(second.descriptions_updated, static_cast<size_t>(1));   // only Kaguya
    CHECK_EQ(second.descriptions_added, static_cast<size_t>(0));
    CHECK_EQ(second.categories_added, static_cast<size_t>(0));
    CHECK_EQ(vault::find_tag_description(s, "artist:Kaguya"), std::string_view("revised"));
    CHECK_EQ(vault::find_tag_description(s, "landscape"), std::string_view("outdoors"));
    CHECK_EQ(s.tag_descriptions.size(), static_cast<size_t>(2));     // no third row
}

// The strongest form: the second import happens after the settings have made a
// full round trip through the .osv. Identity has to survive serialise/deserialise
// — first-seen casing, swatch and key order all come back as they went in — or a
// re-import would silently duplicate rows in a vault the owner has reopened.
TEST(tag_dict_import_reimport_after_lock_and_reopen_changes_nothing)
{
    static const crypto::KdfParams kdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};
    const std::string pw = "testpw";
    const std::span<const uint8_t> pw_bytes{reinterpret_cast<const uint8_t*>(pw.data()), pw.size()};

    TempVault tv;
    REQUIRE(vault::Vault::create(tv.path.string(), pw_bytes, {}, kdf, tv.v) ==
            vault::VaultResult::Ok);

    // Deliberately mixed casing, and one category the seeded vault does NOT have
    // ("Studio") next to one it does ("artist"), so both the new-category and the
    // existing-category paths have to survive the round trip.
    const auto parsed = from({e("Artist", "Kaguya", "Doujin artist"),
                              e("Studio", "Ghibli", "Animation studio"),
                              e("", "Landscape", "outdoors")});

    auto       settings = vault::vault_settings(tv.v);
    const auto first    = apply_tag_dict(settings, parsed);
    CHECK_EQ(first.categories_added, static_cast<size_t>(1));    // Studio only
    CHECK_EQ(first.descriptions_added, static_cast<size_t>(3));
    REQUIRE(vault::set_vault_settings(tv.v, std::move(settings)) == vault::VaultResult::Ok);

    tv.v.lock();
    REQUIRE(vault::Vault::open(tv.path.string(), tv.v) == vault::VaultResult::Ok);
    REQUIRE(tv.v.unlock(pw_bytes, {}) == vault::VaultResult::Ok);

    auto                reopened = vault::vault_settings(tv.v);
    const VaultSettings before   = reopened;
    const auto          second   = apply_tag_dict(reopened,
                                                  from({e("ARTIST", "kaguya", "Doujin artist"),
                                                        e("STUDIO", "GHIBLI", "Animation studio"),
                                                        e("", "LANDSCAPE", "outdoors")}));

    CHECK_EQ(second.categories_added, static_cast<size_t>(0));
    CHECK_EQ(second.descriptions_added, static_cast<size_t>(0));
    CHECK_EQ(second.descriptions_updated, static_cast<size_t>(0));
    CHECK(reopened.categories == before.categories);
    CHECK(reopened.tag_descriptions == before.tag_descriptions);

    // A fresh vault is SEEDED with "artist" (lowercase, swatch 0), so the file's
    // "Artist" matched something that already existed and the vault's spelling
    // stands — an import never restyles or recolours a category it did not create.
    CHECK_EQ(swatch_of(reopened, "artist"), 0);
    CHECK_EQ(swatch_of(reopened, "Artist"), -1);
    // "Studio" was genuinely new, so it kept the file's spelling and took the
    // lowest free swatch above the eight seeded ones.
    CHECK_EQ(swatch_of(reopened, "Studio"), 8);

    // Description keys are a separate store from the category rows: this key was
    // first seen here, so it keeps the file's casing even though the category row
    // says "artist". Both are matched case-insensitively, so they cannot diverge.
    REQUIRE(reopened.tag_descriptions.size() == 3);
    CHECK_EQ(reopened.tag_descriptions[0].tag, std::string("Artist:Kaguya"));
    CHECK_EQ(reopened.tag_descriptions[1].tag, std::string("Studio:Ghibli"));
    CHECK_EQ(reopened.tag_descriptions[2].tag, std::string("Landscape"));
}
