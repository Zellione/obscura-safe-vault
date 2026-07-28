// Phase 56: the right mouse button is a universal "back / up one level". Rather
// than teaching every screen and modal a second cancel path, the App translates
// a right button-down into a synthetic Escape at the single event funnel, so
// every surface's existing Esc handling is reused verbatim.

#include "test_framework.h"

#include <SDL3/SDL.h>

#include "app/back_click.h"

namespace {
SDL_Event button_event(SDL_EventType type, uint8_t button)
{
    SDL_Event e{};
    e.type          = type;
    e.button.type   = type;
    e.button.button = button;
    e.button.x      = 100.0f;
    e.button.y      = 200.0f;
    return e;
}
} // namespace

TEST(right_button_down_is_a_back_click)
{
    CHECK(app::is_back_click(button_event(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT)));
}

TEST(left_and_middle_buttons_are_not_back_clicks)
{
    CHECK_FALSE(app::is_back_click(button_event(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT)));
    CHECK_FALSE(app::is_back_click(button_event(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_MIDDLE)));
}

TEST(a_right_button_release_is_swallowed_not_translated)
{
    const SDL_Event up = button_event(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_RIGHT);
    CHECK_FALSE(app::is_back_click(up));
    CHECK(app::is_back_click_release(up));
}

TEST(a_left_button_release_is_delivered_normally)
{
    CHECK_FALSE(app::is_back_click_release(button_event(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT)));
}

TEST(the_synthetic_event_is_an_unmodified_escape_key_press)
{
    const SDL_Event k = app::make_back_key_event();
    CHECK_EQ(k.type, static_cast<uint32_t>(SDL_EVENT_KEY_DOWN));
    CHECK_EQ(k.key.key, SDLK_ESCAPE);
    CHECK_EQ(k.key.scancode, SDL_SCANCODE_ESCAPE);
    CHECK_EQ(k.key.mod, static_cast<SDL_Keymod>(SDL_KMOD_NONE));
    CHECK_FALSE(k.key.repeat);
    CHECK(k.key.down);
}

TEST(a_non_mouse_event_is_never_a_back_click)
{
    SDL_Event k{};
    k.type    = SDL_EVENT_KEY_DOWN;
    k.key.key = SDLK_ESCAPE;
    CHECK_FALSE(app::is_back_click(k));
    CHECK_FALSE(app::is_back_click_release(k));
}
