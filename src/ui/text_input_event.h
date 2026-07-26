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

#include <vector>

#include "ui/help_popup.h"

namespace ui {

class ITextInput;

// Apply `e` to `field`. Returns true if the event was consumed and the caller
// must not process it further.
[[nodiscard]] bool handle_text_input_event(ITextInput& field, const SDL_Event& e);

// Routing predicate for the screens whose fields have EMPTY-buffer semantics of
// their own: the advanced-search builder (Left/Right switch tag group, Del
// removes a committed tag, Backspace drops the last chip) and the tag editor.
//
// With text in the buffer, the field owns every editing key. With the buffer
// empty there is nothing to edit, so only the keys that can put text INTO it —
// typing and pasting — are the field's; everything else falls through to the
// host's own handling. Hosts without such semantics can call
// handle_text_input_event() unconditionally instead.
[[nodiscard]] bool field_owns_event(const ITextInput& field, const SDL_Event& e) noexcept;

// The F1 help entries for the editing keys above. Shared so every screen that
// hosts a text field documents them identically; append it to the screen's own
// help_groups().
[[nodiscard]] HelpGroup text_editing_help_group();

} // namespace ui
