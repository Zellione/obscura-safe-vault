#include "ui/input.h"

namespace ui {

InputAction map_key(SDL_Keycode key, SDL_Keymod /*mods*/) noexcept
{
    switch (key) {
        case SDLK_LEFT:      return InputAction::NavLeft;
        case SDLK_RIGHT:     return InputAction::NavRight;
        case SDLK_UP:        return InputAction::NavUp;
        case SDLK_DOWN:      return InputAction::NavDown;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:     return InputAction::Select;
        case SDLK_BACKSPACE:
        case SDLK_ESCAPE:    return InputAction::Back;
        case SDLK_I:         return InputAction::Import;
        case SDLK_N:         return InputAction::NewGallery;
        default:             return InputAction::None;
    }
}

} // namespace ui
