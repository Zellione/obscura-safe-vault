// Phase 56: layout is in render-output PIXELS, SDL mouse events arrive in window
// POINTS. On a 150%-scaled Windows display those spaces differ by 1.5, so every
// hit-test — tile clicks, edge-click nav, the video seek bar, the footer band —
// landed at two-thirds of the cursor's real position.

#include "test_framework.h"

#include <SDL3/SDL.h>

#include "gfx/window.h"

TEST(mouse_scaling_is_the_identity_at_unit_density)
{
    SDL_Event e{};
    e.type     = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.x = 640.0f;
    e.button.y = 400.0f;
    gfx::scale_mouse_event(e, 1.0f);
    CHECK_EQ(e.button.x, 640.0f);
    CHECK_EQ(e.button.y, 400.0f);
}

TEST(button_positions_scale_into_render_pixels)
{
    SDL_Event e{};
    e.type     = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.x = 640.0f;
    e.button.y = 400.0f;
    gfx::scale_mouse_event(e, 1.5f);
    CHECK_EQ(e.button.x, 960.0f);
    CHECK_EQ(e.button.y, 600.0f);
}

TEST(motion_scales_its_deltas_as_well_as_its_position)
{
    SDL_Event e{};
    e.type       = SDL_EVENT_MOUSE_MOTION;
    e.motion.x    = 100.0f;
    e.motion.y    = 50.0f;
    e.motion.xrel = 10.0f;
    e.motion.yrel = -4.0f;
    gfx::scale_mouse_event(e, 2.0f);
    CHECK_EQ(e.motion.x, 200.0f);
    CHECK_EQ(e.motion.y, 100.0f);
    CHECK_EQ(e.motion.xrel, 20.0f);
    CHECK_EQ(e.motion.yrel, -8.0f);
}

TEST(wheel_scales_its_cursor_position_but_not_its_scroll_amount)
{
    SDL_Event e{};
    e.type          = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.mouse_x = 300.0f;
    e.wheel.mouse_y = 150.0f;
    e.wheel.y       = 1.0f;
    gfx::scale_mouse_event(e, 1.25f);
    CHECK_EQ(e.wheel.mouse_x, 375.0f);
    CHECK_EQ(e.wheel.mouse_y, 187.5f);
    CHECK_EQ(e.wheel.y, 1.0f);      // scroll ticks are not a coordinate
}

TEST(a_key_event_is_left_alone)
{
    SDL_Event e{};
    e.type    = SDL_EVENT_KEY_DOWN;
    e.key.key = SDLK_ESCAPE;
    gfx::scale_mouse_event(e, 2.0f);
    CHECK_EQ(e.key.key, SDLK_ESCAPE);
}

TEST(a_nonsensical_density_is_ignored_rather_than_collapsing_the_ui)
{
    SDL_Event e{};
    e.type     = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.x = 640.0f;
    gfx::scale_mouse_event(e, 0.0f);
    CHECK_EQ(e.button.x, 640.0f);
}
