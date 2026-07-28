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
    SDL_Event e{};
    e.type         = SDL_EVENT_KEY_DOWN;
    e.key.type     = SDL_EVENT_KEY_DOWN;
    e.key.key      = SDLK_ESCAPE;
    e.key.scancode = SDL_SCANCODE_ESCAPE;
    e.key.mod      = SDL_KMOD_NONE;
    e.key.down     = true;
    e.key.repeat   = false;
    return e;
}

} // namespace app
