#pragma once

// Phase 49: pure settings-overlay state — section rail, row navigation, value
// cycling and category CRUD. SDL-free.

#include <string>

#include "gfx/theme.h"
#include "vault/index.h"

namespace ui {

enum class SettingsSection : uint8_t { Appearance = 0, Browsing, TagColours };
inline constexpr int SETTINGS_SECTION_COUNT = 3;

struct SettingsState {
    SettingsSection    section           = SettingsSection::Appearance;
    bool               in_pane           = false;      // false: focus is on the section rail
    int                row               = 0;          // focused row within the pane
    bool               open              = false;
    bool               vault_unlocked    = false;
    vault::VaultSettings draft;
    gfx::ThemeId       theme             = gfx::ThemeId::RefinedSlate;
};

// Navigate between sections; clamp to [0, SETTINGS_SECTION_COUNT). Reset row to 0.
void settings_move_section(SettingsState& state, int delta) noexcept;

// Navigate within rows of the current section; clamp to [0, row_count).
void settings_move_row(SettingsState& state, int delta) noexcept;

// Change the value at the focused (section, row): cycle theme, sort key, toggle
// flag, or wrap swatch.
void settings_change_value(SettingsState& state, int delta) noexcept;

// Row count for the focused section.
[[nodiscard]] int settings_row_count(const SettingsState& state) noexcept;

// Add a category (trimmed, non-empty, non-duplicate, within size cap). Swatch
// defaults to size() % TAG_SWATCH_COUNT.
[[nodiscard]] bool settings_add_category(SettingsState& state, std::string name);

// Remove category at the given index (no-op if out of range); clamp row if needed.
void settings_remove_category(SettingsState& state, int row) noexcept;

// Rename category at the given index, with duplicate detection. Returns false if
// the new name matches another category (case-insensitive) or if renaming to
// itself.
[[nodiscard]] bool settings_rename_category(SettingsState& state, int row,
                                            std::string name);

} // namespace ui
