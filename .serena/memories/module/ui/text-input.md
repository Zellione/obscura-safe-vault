# Module: ui/text-input — text fields, input handling, clipboard

Shared text-input infrastructure used by every field in the app.

## Text input (Phase 54) — every field in the app routes through these
- `text_input_model.*` — the shared editing model. `ITextInput` (storage-agnostic
  interface) → `TextInputBase` (ALL caret/selection/word/UTF-8 logic, expressed over
  two storage primitives `bytes()` + `splice()`) → `TextInputModel` (std::string).
  The base is the point: the ordinary and secure backends cannot drift, because
  neither implements any navigation of its own. Also the pure UTF-8/word helpers
  (`utf8_prev_boundary`/`utf8_next_boundary`/`utf8_char_count`/`word_boundary_*`)
  and input sanitising (`acceptable_input_run` — valid UTF-8, no C0 controls or
  DEL, since fields are single-line and a pasted newline must not enter).
  `revision()` is a monotonic counter bumped on CONTENT changes only; hosts whose
  field drives a live filter/autosuggest compare it across the shared handler.
  A size comparison would miss replacing a selection with same-length text.
  `insert()` splices the caller's buffer run-by-run and never allocates an
  intermediate — that is what lets the secure backend honour "no std::string".
- `secure_text_input.*` — `SecureTextInput`, the same interface over a fixed-capacity
  mlock'd `crypto::SecureBytes`. Every splice `crypto_wipe`s the bytes the shift
  vacates; `clear()` wipes the whole buffer. `selection_text()` ALWAYS returns
  empty — copy/cut out of a password field is refused by design (invariant #2 +
  the Phase 45 threat model), and this is the second line of defence behind the
  event handler. `text_view()` aliases the locked bytes for the KDF/strength
  call sites; never copy it into a std::string. **Replaced `SecureTextField`,
  which is deleted.**
- `text_field_view.*` — pure layout: `layout_text_field(text, caret, sel, field_w,
  prev_scroll, measure)` → visible run + caret x + selection rect + scroll,
  following the caret at both edges. Partial glyphs are excluded at both ends so
  the run draws without a clip rect. O(n) via a one-pass prefix-width table (the
  atlas has no kerning, so per-character advances sum exactly). `caret_is_on(now_ms,
  last_edit_ms)` is the blink — a pure function of a monotonic clock, NOT a
  per-frame dt, so no dialog has to grow an `update()` just to blink a caret.
- `clipboard.*` — `ClipboardBackend` seam (SDL-backed by default,
  `set_clipboard_backend()` injects a mock) + `paste_from_clipboard` /
  `copy_selection_to_clipboard` / `cut_selection_to_clipboard`. Paste views the
  backend's buffer directly into mlock'd storage; the SDL backend `crypto_wipe`s
  that buffer in `release_text()` before `SDL_free`. Copy/cut return false for a
  secure field. Orthogonal to `clipboard_secret.h` (Phase 45 auto-clear timer).
- `text_input_event.*` — `handle_text_input_event(ITextInput&, SDL_Event&)`, the ONE
  handler. **Key precedence: a focused field consumes Ctrl+A/C/X/V before its host
  screen** (else Phase 53's gallery Ctrl+A fires while the user selects typed text).
  Does NOT consume Enter/Esc/Tab/Up/Down or Ctrl+Up/Ctrl+Down (detail-panel scroll).
  `field_owns_event()` is the routing predicate for hosts whose EMPTY buffer has its
  own key meanings (advanced-search builder chips, tag editor, tag-overview filter):
  with text present the field owns every editing key, with it empty only typing and
  pasting are the field's. `text_editing_help_group()` is the shared F1 entry the
  four field-owning screens append.
- Draw side lives in `widgets.*`: `TextFieldChrome` (per-field scroll + blink state,
  one extra member per migrated field), `draw_edit_field` (boxed) and
  `draw_inline_edit_text` (bare run, for the inline-laid-out fields). Both paint
  through one private helper so they cannot disagree about caret placement. A
  masked field lays out one `*` per CHARACTER, not per byte. The helper detects an
  edit by comparing against what it drew last frame, so hosts never report edits.
- Tests: `tests/ui/text_input_conformance.h` holds the storage-agnostic suite and
  BOTH `test_text_input_model.cpp` and `test_secure_text_input.cpp` run it — a
  per-backend copy is how the two would diverge. Plus `test_text_field_view.cpp`,
  `test_clipboard.cpp` (mock backend), `test_text_input_precedence.cpp`.
