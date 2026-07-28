#pragma once

// Phase 56: right mouse button = "back / up one level".
//
// Rather than adding a second cancel path to every screen, dialog and modal,
// App translates a right button-down into a synthetic Escape key-down at the
// single event funnel (App::dispatch_event) and dispatches that instead. Every
// surface's existing Esc handling is reused verbatim, so the behaviour cannot
// drift between them: the grid clears a selection then ascends, the viewer
// leaves fullscreen then returns to the gallery, and any open modal cancels.

#include <SDL3/SDL.h>

namespace app {

// True for a right mouse button PRESS — the event that becomes an Escape.
[[nodiscard]] bool is_back_click(const SDL_Event& e) noexcept;

// True for a right mouse button RELEASE, which is dropped: a screen that never
// saw the press must not see a dangling release.
[[nodiscard]] bool is_back_click_release(const SDL_Event& e) noexcept;

// The synthetic Escape press a back-click is delivered as.
[[nodiscard]] SDL_Event make_back_key_event() noexcept;

} // namespace app
