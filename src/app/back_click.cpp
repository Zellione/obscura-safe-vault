#include "app/back_click.h"

namespace app {

bool is_back_click(const SDL_Event& e) noexcept
{
    return e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT;
}

bool is_back_click_release(const SDL_Event& e) noexcept
{
    return e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT;
}

SDL_Event make_back_key_event() noexcept
{
    // Assigned as a single struct initializer to satisfy C++ union active-member
    // rules: writing e.type then e.key.* would violate strict aliasing.
    // SDL_KeyboardEvent fields in order: type, reserved, timestamp, windowID,
    // which, scancode, key, mod, raw, down, repeat.
    SDL_Event e{};
    e.key = {
        .type     = SDL_EVENT_KEY_DOWN,
        .reserved = 0,
        .timestamp = 0,
        .windowID = 0,
        .which = 0,
        .scancode = SDL_SCANCODE_ESCAPE,
        .key = SDLK_ESCAPE,
        .mod = SDL_KMOD_NONE,
        .raw = 0,
        .down = true,
        .repeat = false,
    };
    return e;
}

} // namespace app
