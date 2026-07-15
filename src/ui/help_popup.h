#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

namespace gfx { class Renderer; class FontAtlas; }

namespace ui {

struct HelpEntry {
    std::string key;
    std::string description;
};

struct HelpGroup {
    std::string            title;
    std::vector<HelpEntry> entries;
};

// The F1 help popup's open/scroll state. Owned by App (one instance shared
// across every screen) — the content it renders (a screen's HelpGroup list)
// comes from Screen::help_groups() each frame.
struct HelpPopupState {
    bool  open   = false;
    float scroll = 0.0f;   // pixels scrolled down within the panel
};

void open_help(HelpPopupState& s);
void close_help(HelpPopupState& s);
void toggle_help(HelpPopupState& s);

// Pure: total rendered line count for `groups` — one per group title, one
// per entry, plus one blank spacer line before every group after the first.
// Used to size scroll clamping without touching SDL/FontAtlas.
[[nodiscard]] int help_line_count(const std::vector<HelpGroup>& groups);

// Pure: clamps `scroll` into [0, max(0, content_h - viewport_h)].
[[nodiscard]] float clamp_help_scroll(float scroll, float content_h, float viewport_h);

// Up/Down/PageUp/PageDown scroll; Esc/Q close. Returns true if the popup was
// open (i.e. the key was consumed) — a no-op returning false while closed.
bool handle_help_key(HelpPopupState& s, SDL_Keycode key);

// Mouse-wheel scroll; no-op while closed.
void handle_help_wheel(HelpPopupState& s, float wheel_y);

// Draws nothing while `s.open` is false. Veils the whole window, draws a
// centred scrollable panel of `groups`, and clamps `s.scroll` against the
// real content height computed from `groups` + the font.
void draw_help_popup(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H,
                     const std::vector<HelpGroup>& groups, HelpPopupState& s);

} // namespace ui
