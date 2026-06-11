#include "ui/input.h"

namespace ui {

InputAction map_key(SDL_Keycode key, SDL_Keymod /*mods*/) noexcept
{
    using enum InputAction;
    switch (key) {
        case SDLK_LEFT:      return NavLeft;
        case SDLK_RIGHT:     return NavRight;
        case SDLK_UP:        return NavUp;
        case SDLK_DOWN:      return NavDown;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:     return Select;
        case SDLK_BACKSPACE:
        case SDLK_ESCAPE:    return Back;
        case SDLK_I:         return Import;
        case SDLK_N:         return NewGallery;
        default:             return None;
    }
}

} // namespace ui
