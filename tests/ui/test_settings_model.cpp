// Phase 49: pure settings-overlay state — section rail, row navigation, value
// cycling and category CRUD. SDL-free.

#include "test_framework.h"
#include "ui/settings_model.h"

#include <string>

namespace {

ui::SettingsState unlocked_state()
{
    ui::SettingsState s;
    s.open           = true;
    s.vault_unlocked = true;
    s.draft          = vault::VaultSettings::seeded();
    return s;
}

} // namespace

TEST(settings_section_navigation_clamps)
{
    ui::SettingsState s = unlocked_state();
    ui::settings_move_section(s, -1);
    CHECK(s.section == ui::SettingsSection::Appearance);   // already at the top
    ui::settings_move_section(s, 1);
    CHECK(s.section == ui::SettingsSection::Browsing);
    ui::settings_move_section(s, 99);
    CHECK(s.section == ui::SettingsSection::TagColours);   // clamped at the end
}

TEST(settings_section_change_resets_the_row)
{
    ui::SettingsState s = unlocked_state();
    s.section = ui::SettingsSection::TagColours;
    s.row     = 5;
    ui::settings_move_section(s, -1);
    CHECK_EQ(s.row, 0);
}

TEST(settings_row_count_per_section)
{
    ui::SettingsState s = unlocked_state();
    s.section = ui::SettingsSection::Appearance;
    CHECK_EQ(ui::settings_row_count(s), 1);                // theme
    s.section = ui::SettingsSection::Browsing;
    CHECK_EQ(ui::settings_row_count(s), 2);                // default sort + tile tags
    s.section = ui::SettingsSection::TagColours;
    CHECK_EQ(ui::settings_row_count(s), 8);                // the seeded categories
}

TEST(settings_locked_vault_has_no_rows_in_vault_sections)
{
    ui::SettingsState s = unlocked_state();
    s.vault_unlocked = false;
    s.section = ui::SettingsSection::Browsing;
    CHECK_EQ(ui::settings_row_count(s), 0);
    s.section = ui::SettingsSection::TagColours;
    CHECK_EQ(ui::settings_row_count(s), 0);
    s.section = ui::SettingsSection::Appearance;
    CHECK_EQ(ui::settings_row_count(s), 1);                // theme still works
}

TEST(settings_row_navigation_clamps)
{
    ui::SettingsState s = unlocked_state();
    s.section = ui::SettingsSection::TagColours;
    ui::settings_move_row(s, -1);
    CHECK_EQ(s.row, 0);
    ui::settings_move_row(s, 99);
    CHECK_EQ(s.row, 7);
}

TEST(settings_change_value_cycles_the_default_sort)
{
    ui::SettingsState s = unlocked_state();
    s.section = ui::SettingsSection::Browsing;
    s.row     = 0;
    s.draft.default_sort = vault::SortKey::Insertion;
    ui::settings_change_value(s, 1);
    CHECK(s.draft.default_sort == vault::SortKey::NameAsc);
}

TEST(settings_default_sort_never_cycles_to_default)
{
    // "Default" means "follow the vault default" — as the vault default itself
    // it is meaningless, so the setting skips it.
    ui::SettingsState s = unlocked_state();
    s.section = ui::SettingsSection::Browsing;
    s.row     = 0;
    s.draft.default_sort = vault::SortKey::SizeDesc;
    ui::settings_change_value(s, 1);
    CHECK(s.draft.default_sort == vault::SortKey::Insertion);
    ui::settings_change_value(s, 1);
    CHECK(s.draft.default_sort == vault::SortKey::NameAsc);   // skipped Default
}

TEST(settings_change_value_toggles_the_tile_flag)
{
    ui::SettingsState s = unlocked_state();
    s.section = ui::SettingsSection::Browsing;
    s.row     = 1;
    CHECK(s.draft.tiles_show_tags);
    ui::settings_change_value(s, 1);
    CHECK(!s.draft.tiles_show_tags);
}

TEST(settings_change_value_wraps_the_swatch)
{
    ui::SettingsState s = unlocked_state();
    s.section = ui::SettingsSection::TagColours;
    s.row     = 0;
    s.draft.categories[0].swatch = 0;
    ui::settings_change_value(s, -1);
    CHECK_EQ(s.draft.categories[0].swatch, gfx::TAG_SWATCH_COUNT - 1);
    ui::settings_change_value(s, 1);
    CHECK_EQ(s.draft.categories[0].swatch, 0);
}

TEST(settings_add_category_rejects_blank_and_duplicate)
{
    ui::SettingsState s = unlocked_state();
    CHECK(!ui::settings_add_category(s, ""));
    CHECK(!ui::settings_add_category(s, "   "));
    CHECK(!ui::settings_add_category(s, "ARTIST"));          // ci duplicate
    CHECK(ui::settings_add_category(s, "studio"));
    CHECK_EQ(s.draft.categories.size(), 9);
    CHECK(s.draft.categories.back().name == "studio");
}

TEST(settings_add_category_trims_and_respects_the_cap)
{
    ui::SettingsState s = unlocked_state();
    CHECK(ui::settings_add_category(s, "  studio  "));
    CHECK(s.draft.categories.back().name == "studio");
    CHECK(!ui::settings_add_category(s,
        std::string(vault::INDEX_MAX_CATEGORY_BYTES + 1, 'x')));
}

TEST(settings_add_category_refuses_past_the_limit)
{
    ui::SettingsState s = unlocked_state();
    s.draft.categories.clear();
    for (size_t i = 0; i < vault::INDEX_MAX_TAG_CATEGORIES; ++i) {
        CHECK(ui::settings_add_category(s, "c" + std::to_string(i)));
    }
    CHECK(!ui::settings_add_category(s, "one-too-many"));
}

TEST(settings_remove_category_clamps_the_row)
{
    ui::SettingsState s = unlocked_state();
    s.section = ui::SettingsSection::TagColours;
    s.row     = 7;
    ui::settings_remove_category(s, 7);
    CHECK_EQ(s.draft.categories.size(), 7);
    CHECK_EQ(s.row, 6);                                      // focus stays valid
    ui::settings_remove_category(s, 99);                     // out of range: no-op
    CHECK_EQ(s.draft.categories.size(), 7);
}

TEST(settings_rename_category_rejects_a_duplicate)
{
    ui::SettingsState s = unlocked_state();
    CHECK(!ui::settings_rename_category(s, 0, "PARODY"));     // collides with row 2
    CHECK(ui::settings_rename_category(s, 0, "illustrator"));
    CHECK(s.draft.categories[0].name == "illustrator");
    CHECK(!ui::settings_rename_category(s, 0, "illustrator")); // renaming to itself is a no-op fail
}
