#pragma once

// Phase 49: pure settings-overlay state — section rail, row navigation, value
// cycling and category CRUD. SDL-free.

#include <string>

#include "gfx/theme.h"
#include "platform/second_vault_pref.h"
#include "vault/index.h"

#include "ui/gallery_view.h"
#include "ui/text_input_model.h"
#include "ui/widgets.h"

namespace ui {

enum class SettingsSection : uint8_t { Appearance = 0, Browsing, TagColours, VaultOps, Security };
inline constexpr int SETTINGS_SECTION_COUNT = 5;

struct SettingsState {
    SettingsSection    section           = SettingsSection::Appearance;
    bool               in_pane           = false;      // false: focus is on the section rail
    int                row               = 0;          // focused row within the pane
    bool               open              = false;
    bool               vault_unlocked    = false;
    vault::VaultSettings draft;
    gfx::ThemeId       theme             = gfx::ThemeId::RefinedSlate;
    // Phase 84: machine-scoped, like theme
    GalleryView        gallery_view      = GalleryView::GridM;
    // Phase 66: machine-scoped, like theme
    platform::SecondVaultMode second_vault_default = platform::SecondVaultMode::LockNow;
    // Inline "add category" / "rename category" prompt (Phase 49). `prompt_row`
    // is the row being renamed, or -1 when adding.
    bool        prompting  = false;
    int         prompt_row = -1;
    TextInputModel  prompt_buf{vault::INDEX_MAX_CATEGORY_BYTES};
    TextFieldChrome prompt_chrome;   // caret/scroll view state, advanced while drawing
    std::string error;        // one-line failure shown in the overlay footer
    // Phase 65: set by VaultOps section when user presses Enter on "Re-check vault"
    bool        trigger_migration = false;
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

// The keybar drawn in the overlay footer for the current state. Pure so the
// wording is unit-testable; the caller elides it to the panel width.
//
// Phase 83: the arrow keys were written as "[↑↓]" / "[←→]", and the atlas baked
// printable ASCII only — so they rendered as empty brackets. The bundled Noto
// Sans subset has no arrow glyphs at all, so these stay spelled out even now
// that the atlas can draw non-ASCII.
[[nodiscard]] std::string settings_footer_hint(const SettingsState& state);

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
