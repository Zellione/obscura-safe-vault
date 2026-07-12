#include "test_framework.h"

#include <SDL3/SDL.h>

#include "gfx/window.h"

TEST(window_visible_when_no_flags_set)
{
    CHECK(gfx::window_flags_visible(0));
}

TEST(window_not_visible_when_minimized)
{
    CHECK(!gfx::window_flags_visible(SDL_WINDOW_MINIMIZED));
}

TEST(window_not_visible_when_hidden)
{
    CHECK(!gfx::window_flags_visible(SDL_WINDOW_HIDDEN));
}

TEST(window_not_visible_when_occluded)
{
    CHECK(!gfx::window_flags_visible(SDL_WINDOW_OCCLUDED));
}

TEST(window_visible_ignores_unrelated_flags)
{
    CHECK(gfx::window_flags_visible(SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY));
}

TEST(window_not_visible_flags_combine_with_unrelated_flags)
{
    CHECK(!gfx::window_flags_visible(SDL_WINDOW_MINIMIZED | SDL_WINDOW_RESIZABLE));
}
