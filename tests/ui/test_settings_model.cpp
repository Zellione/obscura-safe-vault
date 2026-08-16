// Phase 49: pure settings-overlay state — section rail, row navigation, value
// cycling and category CRUD. SDL-free.

#include "test_framework.h"
#include "ui/settings_model.h"

#include <string>

namespace {

// SettingsState holds a TextInputModel (Phase 54), which is deliberately
// non-copyable and non-movable, so the fixture initialises in place.
void make_unlocked(ui::SettingsState& s)
{
    s.open           = true;
    s.vault_unlocked = true;
    s.draft          = vault::VaultSettings::seeded();
}

} // namespace

TEST(settings_section_navigation_clamps)
{
    ui::SettingsState s;
    make_unlocked(s);
    ui::settings_move_section(s, -1);
    CHECK(s.section == ui::SettingsSection::Appearance);   // already at the top
    ui::settings_move_section(s, 1);
    CHECK(s.section == ui::SettingsSection::Playback);     // Phase 85: inserted after Appearance
    ui::settings_move_section(s, 1);
    CHECK(s.section == ui::SettingsSection::Browsing);     // then Browsing
    ui::settings_move_section(s, 99);
    CHECK(s.section == ui::SettingsSection::Security);   // clamped at the end (Phase 66)
}

TEST(settings_section_change_resets_the_row)
{
    ui::SettingsState s;
    make_unlocked(s);
    s.section = ui::SettingsSection::TagColours;
    s.row     = 5;
    ui::settings_move_section(s, -1);
    CHECK_EQ(s.row, 0);
}

TEST(settings_row_count_per_section)
{
    ui::SettingsState s;
    make_unlocked(s);
    s.section = ui::SettingsSection::Appearance;
    CHECK_EQ(ui::settings_row_count(s), 2);                // theme + default gallery view
    s.section = ui::SettingsSection::Browsing;
    CHECK_EQ(ui::settings_row_count(s), 2);                // default sort + tile tags
    s.section = ui::SettingsSection::TagColours;
    CHECK_EQ(ui::settings_row_count(s), 8);                // the seeded categories
}

TEST(settings_locked_vault_has_no_rows_in_vault_sections)
{
    ui::SettingsState s;
    make_unlocked(s);
    s.vault_unlocked = false;
    s.section = ui::SettingsSection::Browsing;
    CHECK_EQ(ui::settings_row_count(s), 0);
    s.section = ui::SettingsSection::TagColours;
    CHECK_EQ(ui::settings_row_count(s), 0);
    s.section = ui::SettingsSection::Appearance;
    CHECK_EQ(ui::settings_row_count(s), 2);                // theme + default gallery view still work
}

TEST(settings_row_navigation_clamps)
{
    ui::SettingsState s;
    make_unlocked(s);
    s.section = ui::SettingsSection::TagColours;
    ui::settings_move_row(s, -1);
    CHECK_EQ(s.row, 0);
    ui::settings_move_row(s, 99);
    CHECK_EQ(s.row, 7);
}

TEST(settings_change_value_cycles_the_default_sort)
{
    ui::SettingsState s;
    make_unlocked(s);
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
    ui::SettingsState s;
    make_unlocked(s);
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
    ui::SettingsState s;
    make_unlocked(s);
    s.section = ui::SettingsSection::Browsing;
    s.row     = 1;
    CHECK(s.draft.tiles_show_tags);
    ui::settings_change_value(s, 1);
    CHECK(!s.draft.tiles_show_tags);
}

TEST(settings_change_value_wraps_the_swatch)
{
    ui::SettingsState s;
    make_unlocked(s);
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
    ui::SettingsState s;
    make_unlocked(s);
    CHECK(!ui::settings_add_category(s, ""));
    CHECK(!ui::settings_add_category(s, "   "));
    CHECK(!ui::settings_add_category(s, "ARTIST"));          // ci duplicate
    CHECK(ui::settings_add_category(s, "studio"));
    CHECK_EQ(s.draft.categories.size(), 9);
    CHECK(s.draft.categories.back().name == "studio");
}

TEST(settings_add_category_trims_and_respects_the_cap)
{
    ui::SettingsState s;
    make_unlocked(s);
    CHECK(ui::settings_add_category(s, "  studio  "));
    CHECK(s.draft.categories.back().name == "studio");
    CHECK(!ui::settings_add_category(s,
        std::string(vault::INDEX_MAX_CATEGORY_BYTES + 1, 'x')));
}

TEST(settings_add_category_refuses_past_the_limit)
{
    ui::SettingsState s;
    make_unlocked(s);
    s.draft.categories.clear();
    for (size_t i = 0; i < vault::INDEX_MAX_TAG_CATEGORIES; ++i) {
        CHECK(ui::settings_add_category(s, "c" + std::to_string(i)));
    }
    CHECK(!ui::settings_add_category(s, "one-too-many"));
}

TEST(settings_remove_category_clamps_the_row)
{
    ui::SettingsState s;
    make_unlocked(s);
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
    ui::SettingsState s;
    make_unlocked(s);
    CHECK(!ui::settings_rename_category(s, 0, "PARODY"));     // collides with row 2
    CHECK(ui::settings_rename_category(s, 0, "illustrator"));
    CHECK(s.draft.categories[0].name == "illustrator");
    CHECK(!ui::settings_rename_category(s, 0, "illustrator")); // renaming to itself is a no-op fail
}

TEST(settings_default_sort_steps_backwards_and_skips_default)
{
    ui::SettingsState s;
    make_unlocked(s);
    s.section = ui::SettingsSection::Browsing;
    s.row     = 0;
    s.draft.default_sort = vault::SortKey::NameAsc;
    ui::settings_change_value(s, -1);
    CHECK(s.draft.default_sort == vault::SortKey::Insertion);   // skipped Default
}

TEST(settings_security_section_always_has_one_row)
{
    ui::SettingsState st;
    st.section = ui::SettingsSection::Security;
    st.vault_unlocked = false;                  // machine-scoped: no vault needed
    CHECK_EQ(ui::settings_row_count(st), 1);
}

TEST(settings_security_value_cycles_and_wraps_both_ways)
{
    using platform::SecondVaultMode;
    ui::SettingsState st;
    st.section = ui::SettingsSection::Security;
    st.in_pane = true;
    CHECK(st.second_vault_default == SecondVaultMode::LockNow);
    ui::settings_change_value(st, 1);
    CHECK(st.second_vault_default == SecondVaultMode::KeepTimed);
    ui::settings_change_value(st, 1);
    CHECK(st.second_vault_default == SecondVaultMode::KeepSession);
    ui::settings_change_value(st, 1);           // wraps forward
    CHECK(st.second_vault_default == SecondVaultMode::LockNow);
    ui::settings_change_value(st, -1);          // wraps backward
    CHECK(st.second_vault_default == SecondVaultMode::KeepSession);
}

// ---------------------------------------------------------------------------
// Phase 83 — the F2 footer keybar
//
// The arrow keys used to read "[↑↓] Move  [←→] Change". The glyph atlas baked
// printable ASCII only and silently skipped every other byte, so the owner saw
// "[] Move  [] Change" — empty brackets. Extending the atlas is not enough
// here: the bundled Noto Sans subset carries no arrow glyphs at all, so the
// keys stay spelled out. This test is the guard against a well-meaning revert.
// ---------------------------------------------------------------------------

TEST(settings_footer_hint_is_free_of_unrenderable_glyphs)
{
    ui::SettingsState st;
    make_unlocked(st);

    for (int variant = 0; variant < 3; ++variant) {
        st.section   = (variant == 1) ? ui::SettingsSection::TagColours
                                      : ui::SettingsSection::Appearance;
        st.prompting = (variant == 2);

        const std::string hint = ui::settings_footer_hint(st);
        CHECK(!hint.empty());
        for (unsigned char ch : hint) CHECK(ch >= 32 && ch < 127);
    }
}

TEST(settings_footer_hint_names_the_arrow_keys)
{
    ui::SettingsState st;
    make_unlocked(st);
    st.section = ui::SettingsSection::TagColours;

    const std::string hint = ui::settings_footer_hint(st);
    CHECK(hint.find("Up/Dn") != std::string::npos);
    CHECK(hint.find("Lt/Rt") != std::string::npos);
    // The section-specific keys must survive alongside them.
    CHECK(hint.find("[N] Add") != std::string::npos);
    CHECK(hint.find("[Del] Remove") != std::string::npos);
}

TEST(settings_footer_hint_prompt_state_wins_over_section)
{
    ui::SettingsState st;
    make_unlocked(st);
    st.section   = ui::SettingsSection::TagColours;
    st.prompting = true;
    CHECK(ui::settings_footer_hint(st).find("[Enter] Confirm") != std::string::npos);
}

TEST(settings_appearance_has_gallery_view_row)
{
    ui::SettingsState s;
    s.section = ui::SettingsSection::Appearance;
    CHECK_EQ(ui::settings_row_count(s), 2);     // theme + default gallery view
}

TEST(settings_gallery_view_row_cycles_both_ways)
{
    ui::SettingsState s;
    s.section = ui::SettingsSection::Appearance;
    s.row     = 1;
    s.gallery_view = ui::GalleryView::GridM;
    ui::settings_change_value(s, 1);
    CHECK_EQ(s.gallery_view, ui::GalleryView::GridL);
    ui::settings_change_value(s, -1);
    ui::settings_change_value(s, -1);
    CHECK_EQ(s.gallery_view, ui::GalleryView::GridS);
    // Row 0 still cycles the theme, untouched by the new row.
    s.row = 0;
    const auto theme_before = s.theme;
    ui::settings_change_value(s, 1);
    CHECK(s.theme != theme_before);
    CHECK_EQ(s.gallery_view, ui::GalleryView::GridS);
}

TEST(settings_playback_section_has_one_row_and_toggles_autoplay)
{
    ui::SettingsState s;
    s.section = ui::SettingsSection::Playback;
    CHECK_EQ(ui::settings_row_count(s), 1);
    CHECK(s.autoplay == true);
    s.in_pane = true; s.row = 0;
    ui::settings_change_value(s, +1);
    CHECK(s.autoplay == false);
    ui::settings_change_value(s, -1);
    CHECK(s.autoplay == true);
}

TEST(settings_section_count_includes_playback)
{
    CHECK_EQ(ui::SETTINGS_SECTION_COUNT, 6);
    // Rail order: Appearance, Playback, Browsing, TagColours, VaultOps, Security.
    CHECK(static_cast<int>(ui::SettingsSection::Playback) == 1);
    CHECK(static_cast<int>(ui::SettingsSection::Browsing) == 2);
}
