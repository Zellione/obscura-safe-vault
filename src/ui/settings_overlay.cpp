#include "ui/settings_overlay.h"

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/theme_pref.h"
#include "ui/gallery_sort.h"
#include "ui/settings_model.h"

namespace ui {

void open_settings(SettingsState& state, SettingsSection section)
{
    state.open    = true;
    state.section = section;
    state.row     = 0;
    state.in_pane = false;
    state.prompting = false;
    state.prompt_row = -1;
    state.prompt_buf.clear();
    state.error.clear();
}

void close_settings(SettingsState& state, gfx::Window& window)
{
    state.open = false;
    state.prompting = false;
    state.prompt_row = -1;
    state.prompt_buf.clear();
    state.error.clear();
    SDL_StopTextInput(window.sdl_window());
}

bool handle_settings_event(SettingsState& state, gfx::Window& window,
                           const SDL_Event& e, bool& commit_out)
{
    commit_out = false;
    if (!state.open) return false;

    // While prompting, handle text input and editing
    if (state.prompting) {
        if (e.type == SDL_EVENT_TEXT_INPUT) {
            state.prompt_buf += e.text.text;
            return true;
        }
        if (e.type == SDL_EVENT_KEY_DOWN) {
            switch (e.key.key) {
            case SDLK_BACKSPACE:
                if (!state.prompt_buf.empty()) {
                    state.prompt_buf.pop_back();
                }
                return true;
            case SDLK_RETURN:
            case SDLK_KP_ENTER: {
                bool success;
                if (state.prompt_row == -1) {
                    // Adding a new category
                    success = settings_add_category(state, state.prompt_buf);
                } else {
                    // Renaming an existing category
                    success = settings_rename_category(state, state.prompt_row, state.prompt_buf);
                }
                if (success) {
                    state.prompting = false;
                    state.prompt_row = -1;
                    state.prompt_buf.clear();
                    state.error.clear();
                    SDL_StopTextInput(window.sdl_window());
                    commit_out = true;
                } else {
                    state.error = "Invalid name";
                }
                return true;
            }
            case SDLK_ESCAPE:
                state.prompting = false;
                state.prompt_row = -1;
                state.prompt_buf.clear();
                state.error.clear();
                SDL_StopTextInput(window.sdl_window());
                return true;
            default:
                break;
            }
        }
        // While prompting, swallow all other events
        return true;
    }

    // Not prompting; handle normal overlay keys
    if (e.type != SDL_EVENT_KEY_DOWN) {
        return true;   // modal swallows other events while open
    }

    switch (e.key.key) {
    case SDLK_ESCAPE:
        close_settings(state, window);
        return true;

    case SDLK_TAB:
        state.in_pane = !state.in_pane;
        return true;

    case SDLK_UP:
        if (state.in_pane) {
            settings_move_row(state, -1);
        } else {
            settings_move_section(state, -1);
        }
        return true;

    case SDLK_DOWN:
        if (state.in_pane) {
            settings_move_row(state, 1);
        } else {
            settings_move_section(state, 1);
        }
        return true;

    case SDLK_LEFT:
        if (state.in_pane) {
            // Theme row: no vault check needed
            if (state.section == SettingsSection::Appearance) {
                settings_change_value(state, -1);
                gfx::set_theme(state.theme);
                (void)platform::ThemePref::default_location().save(state.theme);
                commit_out = false;  // theme is persisted by the pref save
            }
            // Vault-backed rows: require vault_unlocked
            else if (state.vault_unlocked) {
                settings_change_value(state, -1);
                commit_out = true;
            }
        }
        return true;

    case SDLK_RIGHT:
        if (state.in_pane) {
            // Theme row: no vault check needed
            if (state.section == SettingsSection::Appearance) {
                settings_change_value(state, 1);
                gfx::set_theme(state.theme);
                (void)platform::ThemePref::default_location().save(state.theme);
                commit_out = false;  // theme is persisted by the pref save
            }
            // Vault-backed rows: require vault_unlocked
            else if (state.vault_unlocked) {
                settings_change_value(state, 1);
                commit_out = true;
            }
        }
        return true;

    case SDLK_N:
        if (state.in_pane && state.vault_unlocked &&
            state.section == SettingsSection::TagColours) {
            state.prompting = true;
            state.prompt_row = -1;
            state.prompt_buf.clear();
            state.error.clear();
            SDL_StartTextInput(window.sdl_window());
        }
        return true;

    case SDLK_R:
        if (state.in_pane && state.vault_unlocked &&
            state.section == SettingsSection::TagColours) {
            const int count = settings_row_count(state);
            if (state.row >= 0 && state.row < count) {
                state.prompting = true;
                state.prompt_row = state.row;
                state.prompt_buf = state.draft.categories[state.row].name;
                state.error.clear();
                SDL_StartTextInput(window.sdl_window());
            }
        }
        return true;

    case SDLK_DELETE:
        if (state.in_pane && state.vault_unlocked &&
            state.section == SettingsSection::TagColours) {
            settings_remove_category(state, state.row);
            commit_out = true;
        }
        return true;

    default:
        break;
    }

    return true;   // modal swallows all keys while open
}

namespace {

constexpr float RADIUS       = 8.0f;
constexpr float RADIUS_SMALL = 4.0f;
constexpr float PAD          = 20.0f;
constexpr float ITEM_H       = 36.0f;
constexpr float GAP          = 8.0f;

}  // namespace

void draw_settings_overlay(gfx::Renderer& r, gfx::FontAtlas& font,
                           float win_w, float win_h, const SettingsState& state)
{
    if (!state.open) return;
    using namespace gfx::theme;

    // Veil
    r.draw_rect({0, 0, win_w, win_h}, gfx::Color{8, 9, 12, 255});

    // Panel dimensions
    const float panel_w = std::min(800.0f, win_w - 80.0f);
    const float panel_h = std::min(600.0f, win_h - 80.0f);
    const float panel_x = (win_w - panel_w) / 2.0f;
    const float panel_y = (win_h - panel_h) / 2.0f;

    // Draw panel background and border
    r.draw_round_rect({panel_x, panel_y, panel_w, panel_h}, RADIUS, SURFACE);
    r.draw_round_rect({panel_x, panel_y, panel_w, panel_h}, RADIUS, BORDER, /*filled*/ false);

    // Title
    r.draw_text(font, panel_x + PAD, panel_y + PAD, "Settings", TEXT);

    // Content area
    const float content_top = panel_y + PAD + 32.0f;
    const float footer_h = ITEM_H + 12.0f;

    // Left rail: sections
    const float rail_w = 120.0f;
    const float rail_x = panel_x + PAD;
    const float rail_y = content_top;

    for (int i = 0; i < SETTINGS_SECTION_COUNT; ++i) {
        const auto sec = static_cast<SettingsSection>(i);
        const float item_y = rail_y + static_cast<float>(i) * (ITEM_H + GAP);

        // Highlight if focused and we're in rail mode
        const bool focused = (!state.in_pane && state.section == sec);
        if (focused) {
            r.draw_round_rect({rail_x, item_y, rail_w, ITEM_H}, RADIUS_SMALL, SURFACE_HI);
        }

        // Section title
        std::string sec_name;
        if (sec == SettingsSection::Appearance) sec_name = "Appearance";
        else if (sec == SettingsSection::Browsing) sec_name = "Browsing";
        else if (sec == SettingsSection::TagColours) sec_name = "Tag Colours";

        r.draw_text(font, rail_x + 8.0f, item_y + 8.0f, sec_name,
                   focused ? TEXT : TEXT_DIM);
    }

    // Right pane: rows for current section
    const float pane_x = rail_x + rail_w + 20.0f;
    const float pane_w = panel_w - PAD - rail_w - 30.0f;

    const int row_count = settings_row_count(state);

    if (row_count == 0 && !state.vault_unlocked &&
        state.section != SettingsSection::Appearance) {
        // Show "Unlock a vault" message for locked vault sections
        r.draw_text(font, pane_x, content_top + 8.0f, "Unlock a vault to configure",
                   TEXT_FAINT);
    } else {
        for (int i = 0; i < row_count; ++i) {
            const float item_y = content_top + static_cast<float>(i) * (ITEM_H + GAP);
            if (item_y >= panel_y + panel_h - footer_h - 12.0f) break;  // off-screen

            const bool focused = (state.in_pane && state.row == i);
            if (focused) {
                r.draw_round_rect({pane_x, item_y, pane_w, ITEM_H}, RADIUS_SMALL, SURFACE_HI);
            }

            // Row label and value
            std::string label;
            std::string value;

            if (state.section == SettingsSection::Appearance) {
                if (i == 0) {
                    label = "Theme";
                    value = gfx::theme_name(state.theme);
                }
            } else if (state.section == SettingsSection::Browsing) {
                if (i == 0) {
                    label = "Default Sort";
                    value = sort_key_label(state.draft.default_sort,
                                          state.draft.default_sort);
                } else if (i == 1) {
                    label = "Show Tags on Tiles";
                    value = state.draft.tiles_show_tags ? "On" : "Off";
                }
            } else if (state.section == SettingsSection::TagColours) {
                if (i >= 0 && i < static_cast<int>(state.draft.categories.size())) {
                    const auto& cat = state.draft.categories[i];
                    // Draw a swatch dot
                    const auto swatch_color = gfx::tag_swatch(cat.swatch);
                    r.draw_round_rect({pane_x + 8.0f, item_y + 8.0f, 16.0f, 16.0f},
                                     RADIUS_SMALL, swatch_color);
                    label = cat.name;
                    value = gfx::tag_swatch_name(cat.swatch);
                }
            }

            r.draw_text(font, pane_x + 30.0f, item_y + 8.0f, label,
                       focused ? TEXT : TEXT_DIM);
            r.draw_text(font, pane_x + pane_w - 100.0f, item_y + 8.0f, value,
                       focused ? ACCENT : TEXT_FAINT);
        }
    }

    // Footer hint line
    const float footer_y = panel_y + panel_h - footer_h;
    std::string hint;
    if (state.prompting) {
        hint = "[Enter] Confirm  [Esc] Cancel  [Backspace] Delete";
    } else if (state.vault_unlocked && state.section == SettingsSection::TagColours) {
        hint = "[Tab] Switch  [↑↓] Move  [←→] Change  [N] Add  [R] Rename  [Del] Remove";
    } else {
        hint = "[Tab] Switch  [↑↓] Move  [←→] Change  [Esc] Close";
    }
    r.draw_text(font, panel_x + PAD, footer_y, hint, TEXT_FAINT);

    // Draw error message if present
    if (!state.error.empty()) {
        r.draw_text(font, panel_x + PAD, footer_y + 18.0f, state.error, DANGER);
    }

    // Draw prompt if active
    if (state.prompting) {
        const float prompt_w = panel_w * 0.6f;
        const float prompt_h = ITEM_H + 24.0f;
        const float prompt_x = (win_w - prompt_w) / 2.0f;
        const float prompt_y = (win_h - prompt_h) / 2.0f;

        r.draw_round_rect({prompt_x, prompt_y, prompt_w, prompt_h}, RADIUS, SURFACE);
        r.draw_round_rect({prompt_x, prompt_y, prompt_w, prompt_h}, RADIUS, ACCENT,
                         /*filled*/ false);

        std::string prompt_title;
        if (state.prompt_row == -1) {
            prompt_title = "Add Category";
        } else {
            prompt_title = "Rename Category";
        }
        r.draw_text(font, prompt_x + 12.0f, prompt_y + 8.0f, prompt_title, TEXT);

        // Input field
        const float input_y = prompt_y + 32.0f;
        r.draw_rect({prompt_x + 12.0f, input_y, prompt_w - 24.0f, 28.0f},
                   SURFACE_HI);
        r.draw_text(font, prompt_x + 16.0f, input_y + 4.0f,
                   state.prompt_buf.empty() ? "_" : state.prompt_buf, TEXT);
    }
}

} // namespace ui
