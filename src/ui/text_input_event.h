#pragma once

// The one SDL event handler every text field in the app routes through.
//
// KEY PRECEDENCE RULE — a focused text field consumes Ctrl+A / Ctrl+C / Ctrl+X /
// Ctrl+V BEFORE its host screen sees them. Without that, Phase 53's gallery
// Ctrl+A (select all tiles) fires while the user is trying to select the text
// they just typed into the new-gallery prompt. Hosts must therefore call
// handle_text_input_event() first and return early when it consumes the event.
//
// Deliberately NOT consumed, so screen logic keeps working while a field is
// focused: Enter, Esc, Tab, Up/Down (list navigation, autosuggest), and
// Ctrl+Up/Ctrl+Down (Phase 48 detail-panel scroll).
//
// Secure fields consume Ctrl+C and Ctrl+X as no-ops — consumed so the shortcut
// never leaks through to the screen, no-op so the password never leaves the
// field. See ui/secure_text_input.h.

#include <SDL3/SDL.h>

namespace ui {

class ITextInput;

// Apply `e` to `field`. Returns true if the event was consumed and the caller
// must not process it further.
[[nodiscard]] bool handle_text_input_event(ITextInput& field, const SDL_Event& e);

} // namespace ui
