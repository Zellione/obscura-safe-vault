#pragma once

#include <SDL3/SDL.h>

namespace ui {

enum class InputAction {
    None,
    NavLeft, NavRight, NavUp, NavDown,
    Select,      // Enter / KP-Enter / Space
    Back,        // Backspace / Escape
    Import,      // I
    NewGallery,  // N
    // Phase 6: ZoomIn, ZoomOut, ...
};

// Pure mapping of a key (modifiers reserved for later phases) to a UI action.
[[nodiscard]] InputAction map_key(SDL_Keycode key, SDL_Keymod mods) noexcept;

// True for the one key the image viewer routes to animated-GIF playback (Space,
// pause/resume). Every other key must fall through to the normal image handling
// so navigation still works while a GIF is on screen (Phase 47).
[[nodiscard]] bool gif_viewer_consumes_key(SDL_Keycode key) noexcept;

} // namespace ui
