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

} // namespace ui
