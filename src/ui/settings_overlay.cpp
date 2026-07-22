#include "ui/settings_overlay.h"

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/theme_pref.h"
#include "ui/gallery_sort.h"
#include "ui/settings_model.h"

namespace ui {

namespace {

// Handle text input and editing while a prompt is active.
[[nodiscard]] bool handle_prompt_event(SettingsState& state, const gfx::Window& window,
                                       const SDL_Event& e, bool& commit_out)
{
    if (e.type == SDL_EVENT_TEXT_INPUT) {
        state.prompt_buf += e.text.text;
        return true;
    }
    if (e.type == SDL_EVENT_KEY_DOWN) {
        switch (e.key.key) {
        case SDLK_BACKSPACE: {
            if (!state.prompt_buf.empty()) {
                state.prompt_buf.pop_back();
            }
            return true;
        }
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

// Handle navigation and value changes in normal (non-prompting) mode.
[[nodiscard]] bool handle_navigation_event(SettingsState& state, const gfx::Window& window,
                                           SDL_Keycode key)
{
    switch (key) {
    case SDLK_ESCAPE: {
        close_settings(state, window);
        return true;
    }
    case SDLK_TAB: {
        state.in_pane = !state.in_pane;
        return true;
    }
    case SDLK_UP: {
        if (state.in_pane) {
            settings_move_row(state, -1);
        } else {
            settings_move_section(state, -1);
        }
        return true;
    }
    case SDLK_DOWN: {
        if (state.in_pane) {
            settings_move_row(state, 1);
        } else {
            settings_move_section(state, 1);
        }
        return true;
    }
    default:
        break;
    }
    return false;
}

// Handle value changes (LEFT/RIGHT arrows).
[[nodiscard]] bool handle_value_change(SettingsState& state, SDL_Keycode key, bool& commit_out)
{
    if (key == SDLK_LEFT) {
        if (state.in_pane) {
            if (state.section == SettingsSection::Appearance) {
                settings_change_value(state, -1);
                gfx::set_theme(state.theme);
                (void)platform::ThemePref::default_location().save(state.theme);
                commit_out = false;  // theme is persisted by the pref save
            } else if (state.vault_unlocked) {
                settings_change_value(state, -1);
                commit_out = true;
            }
        }
        return true;
    }
    if (key == SDLK_RIGHT) {
        if (state.in_pane) {
            if (state.section == SettingsSection::Appearance) {
                settings_change_value(state, 1);
                gfx::set_theme(state.theme);
                (void)platform::ThemePref::default_location().save(state.theme);
                commit_out = false;  // theme is persisted by the pref save
            } else if (state.vault_unlocked) {
                settings_change_value(state, 1);
                commit_out = true;
            }
        }
        return true;
    }
    return false;
}

// Handle category operations (N/R/DELETE).
[[nodiscard]] bool handle_category_crud(SettingsState& state, const gfx::Window& window,
                                        SDL_Keycode key, bool& commit_out)
{
    if (key == SDLK_N) {
        if (state.in_pane && state.vault_unlocked &&
            state.section == SettingsSection::TagColours) {
            state.prompting = true;
            state.prompt_row = -1;
            state.prompt_buf.clear();
            state.error.clear();
            SDL_StartTextInput(window.sdl_window());
        }
        return true;
    }
    if (key == SDLK_R) {
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
    }
    if (key == SDLK_DELETE) {
        if (state.in_pane && state.vault_unlocked &&
            state.section == SettingsSection::TagColours) {
            settings_remove_category(state, state.row);
            commit_out = true;
        }
        return true;
    }
    return false;
}

}  // namespace

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

void close_settings(SettingsState& state, const gfx::Window& window)
{
    state.open = false;
    state.prompting = false;
    state.prompt_row = -1;
    state.prompt_buf.clear();
    state.error.clear();
    SDL_StopTextInput(window.sdl_window());
}

bool handle_settings_event(SettingsState& state, const gfx::Window& window,
                           const SDL_Event& e, bool& commit_out)
{
    commit_out = false;
    if (!state.open) {
        return false;
    }

    // While prompting, handle text input and editing
    if (state.prompting) {
        return handle_prompt_event(state, window, e, commit_out);
    }

    // Not prompting; handle normal overlay keys
    if (e.type != SDL_EVENT_KEY_DOWN) {
        return true;   // modal swallows other events while open
    }

    // Dispatch to the first handler that consumes the key. Short-circuits, so a
    // key claimed by navigation never reaches value-change or CRUD. The consumed
    // flag is deliberately discarded: an open modal swallows every key regardless
    // of whether a handler acted on it.
    const bool consumed = handle_navigation_event(state, window, e.key.key)
                       || handle_value_change(state, e.key.key, commit_out)
                       || handle_category_crud(state, window, e.key.key, commit_out);
    (void)consumed;
    return true;
}

namespace {

constexpr float RADIUS       = 8.0f;
constexpr float RADIUS_SMALL = 4.0f;
constexpr float PAD          = 20.0f;
constexpr float ITEM_H       = 36.0f;
constexpr float GAP          = 8.0f;

// Draw the left rail showing section titles.
void draw_rail(gfx::Renderer& r, gfx::FontAtlas& font, float rail_x, float rail_y,
               float rail_w, const SettingsState& state)
{
    for (int i = 0; i < SETTINGS_SECTION_COUNT; ++i) {
        const auto sec = static_cast<SettingsSection>(i);
        const float item_y = rail_y + static_cast<float>(i) * (ITEM_H + GAP);

        // Highlight if focused and we're in rail mode
        const bool focused = (!state.in_pane && state.section == sec);
        if (focused) {
            r.draw_round_rect({.x = rail_x, .y = item_y, .w = rail_w, .h = ITEM_H}, RADIUS_SMALL,
                             gfx::theme::SURFACE_HI);
        }

        // Section title
        std::string sec_name;
        if (sec == SettingsSection::Appearance) {
            sec_name = "Appearance";
        } else if (sec == SettingsSection::Browsing) {
            sec_name = "Browsing";
        } else if (sec == SettingsSection::TagColours) {
            sec_name = "Tag Colours";
        }

        r.draw_text(font, rail_x + 8.0f, item_y + 8.0f, sec_name,
                   focused ? gfx::theme::TEXT : gfx::theme::TEXT_DIM);
    }
}

// Draw a single row in the pane for the current section.
void draw_pane_row(gfx::Renderer& r, gfx::FontAtlas& font, float pane_x, float pane_w, float item_y,
                   int row_index, const SettingsState& state)
{
    const bool focused = (state.in_pane && state.row == row_index);
    if (focused) {
        r.draw_round_rect({.x = pane_x, .y = item_y, .w = pane_w, .h = ITEM_H}, RADIUS_SMALL,
                         gfx::theme::SURFACE_HI);
    }

    // Row label and value
    std::string label;
    std::string value;

    if (state.section == SettingsSection::Appearance) {
        if (row_index == 0) {
            label = "Theme";
            value = gfx::theme_name(state.theme);
        }
    } else if (state.section == SettingsSection::Browsing) {
        if (row_index == 0) {
            label = "Default Sort";
            value = sort_key_label(state.draft.default_sort, state.draft.default_sort);
        } else if (row_index == 1) {
            label = "Show Tags on Tiles";
            value = state.draft.tiles_show_tags ? "On" : "Off";
        }
    } else if (state.section == SettingsSection::TagColours) {
        const auto category_count = static_cast<int>(state.draft.categories.size());
        if (row_index < category_count) {
            const auto& cat = state.draft.categories[row_index];
            // Draw a swatch dot
            const auto swatch_color = gfx::tag_swatch(cat.swatch);
            r.draw_round_rect({.x = pane_x + 8.0f, .y = item_y + 8.0f, .w = 16.0f, .h = 16.0f},
                             RADIUS_SMALL, swatch_color);
            label = cat.name;
            value = gfx::tag_swatch_name(cat.swatch);
        }
    }

    r.draw_text(font, pane_x + 30.0f, item_y + 8.0f, label,
               focused ? gfx::theme::TEXT : gfx::theme::TEXT_DIM);
    r.draw_text(font, pane_x + pane_w - 100.0f, item_y + 8.0f, value,
               focused ? gfx::theme::ACCENT : gfx::theme::TEXT_FAINT);
}

// Draw the right pane showing rows for the current section. `pane_bottom` is the
// y past which a row is off-screen and culled.
void draw_pane(gfx::Renderer& r, gfx::FontAtlas& font, float pane_x, float pane_w,
               float content_top, float pane_bottom, const SettingsState& state)
{
    const int row_count = settings_row_count(state);

    if (row_count == 0 && !state.vault_unlocked &&
        state.section != SettingsSection::Appearance) {
        // Show "Unlock a vault" message for locked vault sections
        r.draw_text(font, pane_x, content_top + 8.0f, "Unlock a vault to configure",
                   gfx::theme::TEXT_FAINT);
        return;
    }
    for (int i = 0; i < row_count; ++i) {
        const float item_y = content_top + static_cast<float>(i) * (ITEM_H + GAP);
        if (item_y >= pane_bottom) {
            break;  // off-screen
        }
        draw_pane_row(r, font, pane_x, pane_w, item_y, i, state);
    }
}

// Draw the footer with hint text and error messages.
void draw_footer(gfx::Renderer& r, gfx::FontAtlas& font, float panel_x, float footer_y,
                 const SettingsState& state)
{
    std::string hint;
    if (state.prompting) {
        hint = "[Enter] Confirm  [Esc] Cancel  [Backspace] Delete";
    } else if (state.vault_unlocked && state.section == SettingsSection::TagColours) {
        hint = "[Tab] Switch  [↑↓] Move  [←→] Change  [N] Add  [R] Rename  [Del] Remove";
    } else {
        hint = "[Tab] Switch  [↑↓] Move  [←→] Change  [Esc] Close";
    }
    r.draw_text(font, panel_x + PAD, footer_y, hint, gfx::theme::TEXT_FAINT);

    // Draw error message if present
    if (!state.error.empty()) {
        r.draw_text(font, panel_x + PAD, footer_y + 18.0f, state.error, gfx::theme::DANGER);
    }
}

// Draw the prompt overlay for category name input.
void draw_prompt(gfx::Renderer& r, gfx::FontAtlas& font, float win_w, float win_h,
                 float panel_w, const SettingsState& state)
{
    const float prompt_w = panel_w * 0.6f;
    const float prompt_h = ITEM_H + 24.0f;
    const float prompt_x = (win_w - prompt_w) / 2.0f;
    const float prompt_y = (win_h - prompt_h) / 2.0f;

    r.draw_round_rect({.x = prompt_x, .y = prompt_y, .w = prompt_w, .h = prompt_h}, RADIUS,
                     gfx::theme::SURFACE);
    r.draw_round_rect({.x = prompt_x, .y = prompt_y, .w = prompt_w, .h = prompt_h}, RADIUS,
                     gfx::theme::ACCENT, /*filled*/ false);

    std::string prompt_title;
    if (state.prompt_row == -1) {
        prompt_title = "Add Category";
    } else {
        prompt_title = "Rename Category";
    }
    r.draw_text(font, prompt_x + 12.0f, prompt_y + 8.0f, prompt_title, gfx::theme::TEXT);

    // Input field
    const float input_y = prompt_y + 32.0f;
    r.draw_rect({.x = prompt_x + 12.0f, .y = input_y, .w = prompt_w - 24.0f, .h = 28.0f},
               gfx::theme::SURFACE_HI);
    r.draw_text(font, prompt_x + 16.0f, input_y + 4.0f,
               state.prompt_buf.empty() ? "_" : state.prompt_buf, gfx::theme::TEXT);
}

}  // namespace

void draw_settings_overlay(gfx::Renderer& r, gfx::FontAtlas& font,
                           float win_w, float win_h, const SettingsState& state)
{
    if (!state.open) {
        return;
    }

    // Veil
    r.draw_rect({.x = 0, .y = 0, .w = win_w, .h = win_h}, gfx::Color{.r = 8, .g = 9, .b = 12, .a = 255});

    // Panel dimensions
    const float panel_w = std::min(800.0f, win_w - 80.0f);
    const float panel_h = std::min(600.0f, win_h - 80.0f);
    const float panel_x = (win_w - panel_w) / 2.0f;
    const float panel_y = (win_h - panel_h) / 2.0f;

    // Draw panel background and border
    r.draw_round_rect({.x = panel_x, .y = panel_y, .w = panel_w, .h = panel_h}, RADIUS,
                     gfx::theme::SURFACE);
    r.draw_round_rect({.x = panel_x, .y = panel_y, .w = panel_w, .h = panel_h}, RADIUS,
                     gfx::theme::BORDER, /*filled*/ false);

    // Title
    r.draw_text(font, panel_x + PAD, panel_y + PAD, "Settings", gfx::theme::TEXT);

    // Content area
    const float content_top = panel_y + PAD + 32.0f;
    const float footer_h = ITEM_H + 12.0f;

    // Left rail: sections
    const float rail_w = 120.0f;
    const float rail_x = panel_x + PAD;
    const float rail_y = content_top;

    draw_rail(r, font, rail_x, rail_y, rail_w, state);

    // Right pane: rows for current section
    const float pane_x = rail_x + rail_w + 20.0f;
    const float pane_w = panel_w - PAD - rail_w - 30.0f;

    draw_pane(r, font, pane_x, pane_w, content_top, panel_y + panel_h - footer_h - 12.0f, state);

    // Footer hint line and error message
    const float footer_y = panel_y + panel_h - footer_h;
    draw_footer(r, font, panel_x, footer_y, state);

    // Draw prompt if active
    if (state.prompting) {
        draw_prompt(r, font, win_w, win_h, panel_w, state);
    }
}

} // namespace ui
