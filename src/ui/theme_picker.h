#pragma once

#include <SDL3/SDL.h>

#include "platform/theme_pref.h"   // pulls in gfx::ThemeId

namespace gfx { class Renderer; class FontAtlas; }

namespace ui {

// Overlay to choose the UI colour theme (Phase 23), reachable from the vault
// manager with `C`. Lists the built-in presets; Up/Down applies the highlighted
// theme live (gfx::set_theme) and persists it immediately (theme.conf), so the
// preview *is* the choice. Enter/Esc just close — there is nothing to confirm.
//
// Mirrors the QuickSwitch overlay shape: open()/close()/active() plus a
// consuming handle_event() and a render(). Holds no secrets — only a theme id.
class ThemePicker {
public:
    void open();                                       // select the active theme, activate
    void close() noexcept { active_ = false; }
    [[nodiscard]] bool active() const noexcept { return active_; }

    [[nodiscard]] bool handle_event(const SDL_Event& e);   // true if consumed
    void render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const;

private:
    void select(int sel);                              // clamp, apply live, persist

    platform::ThemePref pref_ = platform::ThemePref::default_location();
    bool                active_ = false;
    int                 sel_    = 0;
};

} // namespace ui
