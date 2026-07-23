#pragma once

#include <SDL3/SDL.h>

namespace gfx { class Renderer; class FontAtlas; class Window; }

namespace ui {

enum class SettingsSection : uint8_t;
struct SettingsState;

// F2 overlay for vault settings (theme, default sort, tiles-show-tags flag, tag
// categories + colours). Opens at a specific section and swallows all input while
// open. Tab toggles focus between section rail and pane; arrows navigate; ←/→
// cycle values; N/R open a category-rename prompt; Del removes a category.

void open_settings(SettingsState& state, SettingsSection section);

// Also stops text input if prompting.
void close_settings(SettingsState& state, const gfx::Window& window);

// Returns true when the event was consumed. Sets `commit_out` when `draft`
// changed and must be persisted by the caller.
[[nodiscard]] bool handle_settings_event(SettingsState& state, const gfx::Window& window,
                                         const SDL_Event& e, bool& commit_out);

void draw_settings_overlay(gfx::Renderer& r, gfx::FontAtlas& font,
                           float win_w, float win_h, const SettingsState& state);

} // namespace ui
