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
