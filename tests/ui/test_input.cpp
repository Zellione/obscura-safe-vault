#include "test_framework.h"

#include <SDL3/SDL.h>

#include "ui/input.h"

TEST(input_arrows)
{
    using ui::InputAction;
    CHECK(ui::map_key(SDLK_LEFT,  SDL_KMOD_NONE) == InputAction::NavLeft);
    CHECK(ui::map_key(SDLK_RIGHT, SDL_KMOD_NONE) == InputAction::NavRight);
    CHECK(ui::map_key(SDLK_UP,    SDL_KMOD_NONE) == InputAction::NavUp);
    CHECK(ui::map_key(SDLK_DOWN,  SDL_KMOD_NONE) == InputAction::NavDown);
}

TEST(input_select_back_commands)
{
    using ui::InputAction;
    CHECK(ui::map_key(SDLK_RETURN,    SDL_KMOD_NONE) == InputAction::Select);
    CHECK(ui::map_key(SDLK_KP_ENTER,  SDL_KMOD_NONE) == InputAction::Select);
    CHECK(ui::map_key(SDLK_SPACE,     SDL_KMOD_NONE) == InputAction::Select);
    CHECK(ui::map_key(SDLK_BACKSPACE, SDL_KMOD_NONE) == InputAction::Back);
    CHECK(ui::map_key(SDLK_ESCAPE,    SDL_KMOD_NONE) == InputAction::Back);
    CHECK(ui::map_key(SDLK_I,         SDL_KMOD_NONE) == InputAction::Import);
    CHECK(ui::map_key(SDLK_N,         SDL_KMOD_NONE) == InputAction::NewGallery);
}

TEST(input_unmapped_is_none)
{
    CHECK(ui::map_key(SDLK_F5, SDL_KMOD_NONE) == ui::InputAction::None);
}

// The image viewer gives an animated GIF exactly one extra key, Space (pause).
// Every other key must fall through to the normal image behaviour — in
// particular the arrows must still navigate between images (Phase 47 bug: the
// GIF branch used to swallow all keys).
TEST(gif_viewer_consumes_only_space)
{
    CHECK(ui::gif_viewer_consumes_key(SDLK_SPACE));
    CHECK(!ui::gif_viewer_consumes_key(SDLK_LEFT));
    CHECK(!ui::gif_viewer_consumes_key(SDLK_RIGHT));
    CHECK(!ui::gif_viewer_consumes_key(SDLK_UP));
    CHECK(!ui::gif_viewer_consumes_key(SDLK_DOWN));
    CHECK(!ui::gif_viewer_consumes_key(SDLK_F));
    CHECK(!ui::gif_viewer_consumes_key(SDLK_P));
    CHECK(!ui::gif_viewer_consumes_key(SDLK_X));
}
