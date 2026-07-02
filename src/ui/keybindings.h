#pragma once

#include <SDL3/SDL.h>

namespace ui {

// Layout-independent keybinding resolution (Phase 25).
//
// Two distinct strategies are needed depending on the shortcut:
//
//  * Character shortcuts (`/` search, `?` advanced search, `` ` `` quick-switch)
//    should fire on whatever PHYSICAL key produces that glyph on the user's
//    layout. SDL_GetKeyFromScancode(scancode, mods, false) resolves the character
//    the active layout + held modifiers actually produce, so e.g. German QWERTZ's
//    Shift+7 (`/`) is matched. These live as is_*_key() helpers below.
//
//  * The `[` / `]` "decrease / increase" pair (in-viewer video volume; slideshow
//    dwell) is the exception: on many non-US layouts those glyphs sit behind AltGr
//    (German QWERTZ: AltGr+8 / AltGr+9), so a character match is awkward-to-
//    impossible to press. Instead they bind to the PHYSICAL key POSITION — the
//    scancode of the two keys right of `P` — via bracket_key_for_scancode(), which
//    is independent of the printed glyph and therefore of the layout. This is the
//    pure, unit-tested mapping.

// The `[` / `]` physical keys as a generic decrease/increase pair, reused for the
// video volume and the slideshow dwell.
enum class BracketKey { None, Decrease, Increase };

// Map a physical key position (scancode) to the decrease/increase pair. Bound to
// the two keys immediately right of `P` (US `[` and `]`), matched by scancode so
// the binding is identical on every keyboard layout — the printed glyph is
// irrelevant. Pure and constexpr: no dependency on the active layout, so it is
// directly unit-testable without an initialised keyboard.
[[nodiscard]] constexpr BracketKey bracket_key_for_scancode(SDL_Scancode sc) noexcept
{
    switch (sc) {
        case SDL_SCANCODE_LEFTBRACKET:  return BracketKey::Decrease;
        case SDL_SCANCODE_RIGHTBRACKET: return BracketKey::Increase;
        default:                        return BracketKey::None;
    }
}

// True when a key-down event resolves to the `/` search glyph on the active
// layout (US: unshifted `/`; German QWERTZ: Shift+7). Matching the produced
// character rather than SDLK_SLASH on the base keycode makes it layout-robust.
[[nodiscard]] inline bool is_search_key(const SDL_KeyboardEvent& key) noexcept
{
    return SDL_GetKeyFromScancode(key.scancode, key.mod, false) == SDLK_SLASH;
}

// True when a key-down event resolves to `?` (Shift+/ on most layouts) — the
// advanced-search shortcut (Phase 18). Layout-robust for the same reason.
[[nodiscard]] inline bool is_advanced_search_key(const SDL_KeyboardEvent& key) noexcept
{
    return SDL_GetKeyFromScancode(key.scancode, key.mod, false) == SDLK_QUESTION;
}

// True when a key-down event is the layout-robust "switch vault" chord (Phase 14).
// The shortcut is documented as `` ` ``, but on e.g. a German layout no key's
// unmodified symbol is `` ` `` (it is a dead accent key), so accept both the
// physical grave key (scancode, left of `1`) and a layout-produced backtick.
[[nodiscard]] inline bool is_quick_switch_key(const SDL_KeyboardEvent& key) noexcept
{
    return key.scancode == SDL_SCANCODE_GRAVE ||
           SDL_GetKeyFromScancode(key.scancode, key.mod, false) == SDLK_GRAVE;
}

} // namespace ui
