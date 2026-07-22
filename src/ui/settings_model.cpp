// Phase 49: pure settings-overlay state — section rail, row navigation, value
// cycling and category CRUD. SDL-free.

#include "ui/settings_model.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

#include "gfx/theme.h"
#include "ui/gallery_sort.h"
#include "ui/tag_inherit.h"
#include "vault/index.h"

namespace ui {

// Trim ASCII whitespace from both ends. Spelled as an explicit character set
// rather than std::isspace: isspace is locale-sensitive and is UB on a negative
// char, and a category name is arbitrary user text.
[[nodiscard]] static std::string trim_whitespace(std::string_view s) noexcept
{
    constexpr std::string_view WS = " \t\n\v\f\r";
    const size_t               first = s.find_first_not_of(WS);
    if (first == std::string_view::npos) {
        return {};
    }
    return std::string(s.substr(first, s.find_last_not_of(WS) - first + 1));
}

void settings_move_section(SettingsState& state, int delta) noexcept
{
    auto current = static_cast<int>(std::to_underlying(state.section));
    int next     = std::clamp(current + delta, 0, SETTINGS_SECTION_COUNT - 1);
    state.section = static_cast<SettingsSection>(next);
    state.row = 0;
}

void settings_move_row(SettingsState& state, int delta) noexcept
{
    int count = settings_row_count(state);
    int next  = std::clamp(state.row + delta, 0, std::max(0, count - 1));
    state.row = next;
}

int settings_row_count(const SettingsState& state) noexcept
{
    using enum SettingsSection;
    switch (state.section) {
    case Appearance:
        return 1; // theme
    case Browsing:
        return state.vault_unlocked ? 2 : 0; // default sort + tiles show tags
    case TagColours: {
        auto size = static_cast<int>(state.draft.categories.size());
        return state.vault_unlocked ? size : 0;
    }
    }
    return 0;
}

namespace {

// Handle theme value change.
void change_theme_value(SettingsState& state, int delta) noexcept
{
    auto current = static_cast<int>(std::to_underlying(state.theme));
    int  next    = (current + delta) % gfx::THEME_COUNT;
    if (next < 0) {
        next += gfx::THEME_COUNT;
    }
    state.theme = static_cast<gfx::ThemeId>(next);
}

// Handle browsing settings value change.
void change_browsing_value(SettingsState& state, int delta) noexcept
{
    if (state.row == 0) {
        // Default sort: cycle with next_sort_key or prev_sort_key, skipping Default
        vault::SortKey current = state.draft.default_sort;
        vault::SortKey next;
        if (delta >= 0) {
            next = next_sort_key(current);
            // Skip if we landed on Default
            if (next == vault::SortKey::Default) {
                next = next_sort_key(next);
            }
        } else {
            next = prev_sort_key(current);
            // Skip if we landed on Default
            if (next == vault::SortKey::Default) {
                next = prev_sort_key(next);
            }
        }
        state.draft.default_sort = next;
    } else if (state.row == 1) {
        // Toggle tiles_show_tags
        state.draft.tiles_show_tags = !state.draft.tiles_show_tags;
    }
}

// Handle tag colour swatch value change.
void change_swatch_value(SettingsState& state, int delta) noexcept
{
    auto size = static_cast<int>(state.draft.categories.size());
    if (state.row >= 0 && state.row < size) {
        auto& swatch = state.draft.categories[state.row].swatch;
        int   current = swatch;
        int   next    = (current + delta) % gfx::TAG_SWATCH_COUNT;
        if (next < 0) {
            next += gfx::TAG_SWATCH_COUNT;
        }
        swatch = static_cast<uint8_t>(next);
    }
}

}  // namespace

void settings_change_value(SettingsState& state, int delta) noexcept
{
    using enum SettingsSection;
    if (state.section == Appearance) {
        change_theme_value(state, delta);
    } else if (state.section == Browsing) {
        change_browsing_value(state, delta);
    } else if (state.section == TagColours) {
        change_swatch_value(state, delta);
    }
}

bool settings_add_category(SettingsState& state, std::string name)
{
    // Trim whitespace
    name = trim_whitespace(name);

    // Reject empty
    if (name.empty()) {
        return false;
    }

    // Reject if too long
    if (name.length() > static_cast<size_t>(vault::INDEX_MAX_CATEGORY_BYTES)) {
        return false;
    }

    // Reject case-insensitive duplicate
    for (const auto& cat : state.draft.categories) {
        if (tag_ci_equal(cat.name, name)) {
            return false;
        }
    }

    // Reject if at capacity
    auto size = static_cast<uint16_t>(state.draft.categories.size());
    if (size >= vault::INDEX_MAX_TAG_CATEGORIES) {
        return false;
    }

    // Append with swatch = size % TAG_SWATCH_COUNT
    vault::TagCategory new_cat;
    new_cat.name   = std::move(name);
    new_cat.swatch = static_cast<uint8_t>(size % gfx::TAG_SWATCH_COUNT);
    state.draft.categories.push_back(std::move(new_cat));
    return true;
}

void settings_remove_category(SettingsState& state, int row) noexcept
{
    if (auto size = static_cast<int>(state.draft.categories.size()); row < 0 || row >= size) {
        return;
    }

    state.draft.categories.erase(state.draft.categories.begin() + row);

    // Clamp row to valid range
    if (!state.draft.categories.empty()) {
        auto new_size = static_cast<int>(state.draft.categories.size() - 1);
        state.row = std::min(state.row, new_size);
    } else {
        state.row = 0;
    }
}

bool settings_rename_category(SettingsState& state, int row, std::string name)
{
    // Validate row
    auto size = static_cast<int>(state.draft.categories.size());
    if (row < 0 || row >= size) {
        return false;
    }

    // Trim whitespace
    name = trim_whitespace(name);

    // Reject empty
    if (name.empty()) {
        return false;
    }

    // Reject if too long
    if (name.length() > static_cast<size_t>(vault::INDEX_MAX_CATEGORY_BYTES)) {
        return false;
    }

    // Reject if renaming to itself (case-insensitive comparison)
    if (const auto& current_name = state.draft.categories[row].name; tag_ci_equal(current_name, name)) {
        return false;
    }

    // Reject case-insensitive duplicate with OTHER categories
    for (int i = 0; i < size; ++i) {
        if (i != row && tag_ci_equal(state.draft.categories[i].name, name)) {
            return false;
        }
    }

    // Update the name
    state.draft.categories[row].name = std::move(name);
    return true;
}

} // namespace ui
