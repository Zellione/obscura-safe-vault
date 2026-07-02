#include "test_framework.h"

#include <SDL3/SDL.h>

#include "ui/keybindings.h"

// The `[` / `]` binding is by PHYSICAL scancode (the two keys right of `P`), so it
// is identical on every keyboard layout — no active-layout dependency, hence these
// checks need no initialised keyboard and are fully deterministic.
TEST(keybindings_bracket_scancode_mapping)
{
    using ui::BracketKey;
    CHECK(ui::bracket_key_for_scancode(SDL_SCANCODE_LEFTBRACKET)  == BracketKey::Decrease);
    CHECK(ui::bracket_key_for_scancode(SDL_SCANCODE_RIGHTBRACKET) == BracketKey::Increase);
}

TEST(keybindings_bracket_ignores_other_scancodes)
{
    using ui::BracketKey;
    // Unrelated physical keys never trigger the decrease/increase action — in
    // particular the German-QWERTZ glyph positions of `[`/`]` (AltGr+8/9) must not.
    CHECK(ui::bracket_key_for_scancode(SDL_SCANCODE_A)         == BracketKey::None);
    CHECK(ui::bracket_key_for_scancode(SDL_SCANCODE_8)         == BracketKey::None);
    CHECK(ui::bracket_key_for_scancode(SDL_SCANCODE_9)         == BracketKey::None);
    CHECK(ui::bracket_key_for_scancode(SDL_SCANCODE_M)         == BracketKey::None);
    CHECK(ui::bracket_key_for_scancode(SDL_SCANCODE_UNKNOWN)   == BracketKey::None);
    CHECK(ui::bracket_key_for_scancode(SDL_SCANCODE_SEMICOLON) == BracketKey::None);
}

// The mapping is usable in a constant expression — a compile-time guarantee that
// it stays free of any runtime/layout state.
TEST(keybindings_bracket_is_constexpr)
{
    static_assert(ui::bracket_key_for_scancode(SDL_SCANCODE_LEFTBRACKET)  == ui::BracketKey::Decrease);
    static_assert(ui::bracket_key_for_scancode(SDL_SCANCODE_RIGHTBRACKET) == ui::BracketKey::Increase);
    static_assert(ui::bracket_key_for_scancode(SDL_SCANCODE_A)            == ui::BracketKey::None);
    CHECK(true);
}

// Volume is reachable three ways so it works however the user presses it.
TEST(keybindings_volume_accepts_bracket_glyph_dash_and_scancode)
{
    using ui::VolumeDir;

    // (1) The `[` / `]` produced character — this is the German-QWERTZ case that
    // regressed: AltGr+8 produces the `[` glyph on the physical `8` key. The caller
    // resolves the character; the physical scancode there is SDL_SCANCODE_8.
    CHECK(ui::volume_dir(SDLK_LEFTBRACKET,  SDL_SCANCODE_8) == VolumeDir::Down);
    CHECK(ui::volume_dir(SDLK_RIGHTBRACKET, SDL_SCANCODE_9) == VolumeDir::Up);

    // (2) The `-` / `+` / `=` glyph keys — direct keys on every layout.
    CHECK(ui::volume_dir(SDLK_MINUS,    SDL_SCANCODE_MINUS)  == VolumeDir::Down);
    CHECK(ui::volume_dir(SDLK_KP_MINUS, SDL_SCANCODE_KP_MINUS) == VolumeDir::Down);
    CHECK(ui::volume_dir(SDLK_PLUS,     SDL_SCANCODE_EQUALS) == VolumeDir::Up);
    CHECK(ui::volume_dir(SDLK_EQUALS,   SDL_SCANCODE_EQUALS) == VolumeDir::Up);
    CHECK(ui::volume_dir(SDLK_KP_PLUS,  SDL_SCANCODE_KP_PLUS) == VolumeDir::Up);

    // (3) The physical `[` / `]` key positions (US bracket keys; German `ü`/`+`),
    // regardless of the produced character.
    CHECK(ui::volume_dir(SDLK_UNKNOWN, SDL_SCANCODE_LEFTBRACKET)  == VolumeDir::Down);
    CHECK(ui::volume_dir(SDLK_UNKNOWN, SDL_SCANCODE_RIGHTBRACKET) == VolumeDir::Up);

    // Unrelated keys do nothing.
    CHECK(ui::volume_dir(SDLK_M, SDL_SCANCODE_M) == VolumeDir::None);
    CHECK(ui::volume_dir(SDLK_8, SDL_SCANCODE_8) == VolumeDir::None);  // plain 8, no AltGr
}
