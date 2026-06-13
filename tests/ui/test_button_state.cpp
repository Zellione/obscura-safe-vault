#include "test_framework.h"

#include <SDL3/SDL.h>

#include "ui/widgets.h"

TEST(button_state_hover_requires_cursor_inside)
{
    const SDL_FRect r{10, 10, 100, 40};
    auto inside  = ui::button_state(r, 20, 20, /*mouse_down*/ false);
    CHECK(inside.hover);
    CHECK_FALSE(inside.active);                 // hovering, not pressed

    auto outside = ui::button_state(r, 200, 200, /*mouse_down*/ false);
    CHECK_FALSE(outside.hover);
    CHECK_FALSE(outside.active);
}

TEST(button_state_active_requires_inside_and_down)
{
    const SDL_FRect r{10, 10, 100, 40};
    auto pressed = ui::button_state(r, 20, 20, /*mouse_down*/ true);
    CHECK(pressed.hover);
    CHECK(pressed.active);

    // Mouse held down but cursor outside -> not active (and not hovering).
    auto elsewhere = ui::button_state(r, 500, 500, /*mouse_down*/ true);
    CHECK_FALSE(elsewhere.hover);
    CHECK_FALSE(elsewhere.active);
}
