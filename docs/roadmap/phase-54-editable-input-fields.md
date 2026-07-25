## Phase 54 — Editable input fields (caret, selection, clipboard) 🔜

**Goal:** Replace append-only text fields with full editing: caret movement, selection,
and paste. UTF-8 aware throughout, no corruption on backspace. Secure fields prevent
copy/cut but accept paste. All eighteen text fields in the app migrate to a shared input
model and event handler for consistency.

Full design, including tradeoffs and technical rationale:
[`docs/superpowers/specs/2026-07-25-phase54-editable-input-fields-design.md`](../superpowers/specs/2026-07-25-phase54-editable-input-fields-design.md).

### Tasks

**1. Pure text input model**
- [ ] `src/ui/text_input_model.h/.cpp` — `TextInputModel` class wrapping `std::string` with byte-offset caret and selection. Operations: `insert()`, `backspace()`, `del()`, `move_left/right(by_word, extend)`, `move_home/end(extend)`, `select_all()`, `selection()`, `delete_selection()`, `set_text()`, `clear()`, with configurable `byte_cap`. UTF-8 aware; word-boundary detection by character class.
- [ ] `tests/ui/test_text_input_model.cpp` — UTF-8 boundary tests (multi-byte backspace/delete without corruption), word-jump edge cases, selection-replace-on-insert, capacity clamp, set_text/clear state reset.

**2. Pure text-field view layout**
- [ ] `src/ui/text_field_view.h/.cpp` — Given text, caret byte-offset, selection range, field width, and a `measure()` callable; return visible substring, caret pixel offset, selection rect, and scroll offset. Pure, SDL-free. Caret blinks on dt, goes solid while typing.
- [ ] `tests/ui/test_text_field_view.cpp` — Scroll keeps caret in view at both edges; selection rect clips to visible region; text elision at field boundaries.

**3. Secure text input (mlock'd storage)**
- [ ] `src/ui/secure_text_input.h/.cpp` — Identical `TextInputModel` interface backed by `crypto::SecureBytes`. Every mutation wipes vacated bytes; `clear()` wipes the buffer. Copy/cut are no-ops; paste is allowed.
- [ ] `tests/ui/test_secure_text_input.cpp` — All `TextInputModel` tests adapted for `SecureTextInput`; PLUS verify vacated bytes are actually zeroed. Run under `scripts/test.sh --asan`.

**4. Clipboard shim**
- [ ] `src/ui/clipboard.h/.cpp` — `get_clipboard_text()` and `set_clipboard_text()` wrapping SDL, behind a seam for testability. For secure fields, copy clipboard directly into mlock'd buffer, wipe SDL's returned buffer before freeing. Ordinary fields use the string directly.
- [ ] `tests/ui/test_clipboard.cpp` — Unit-test behind a mock seam (no SDL); verify secure fields wipe SDL's buffer.

**5. Shared text input event handler**
- [ ] `src/ui/text_input_event.h/.cpp` — `handle_text_input_event()`, a single function taking `TextInputModel&` and `SDL_Event`, dispatching all combinations: Left/Right/Home/End/Ctrl+Left/Ctrl+Right, each with optional Shift to extend selection; Backspace/Delete; Ctrl+A/C/X/V/Shift+Insert. Secure fields suppress copy/cut. Returns true if consumed.
- [ ] Key precedence rule test: `Ctrl+A` in a focused field is consumed before the screen's `Ctrl+A` is called (verified against a mock screen handler).

**6. Retire `SecureTextField` and migrate the four secure fields**
- [ ] Delete `src/ui/secure_text_field.h/.cpp`; fold `tests/ui/test_secure_text_field.cpp` into the new `SecureTextInput` suite so no coverage is lost.
- [ ] `src/ui/unlock_screen.h:54` (`pw_`) and `:55` (`confirm_`) migrate to `SecureTextInput`; event routing through the shared handler.
- [ ] `src/ui/gallery_grid.h:255` (`naming_.password.buf`, handler `src/ui/gallery_grid.cpp:631`) — encrypted-archive password prompt migrates.
- [ ] `src/ui/vault_unlock_picker.h:73` (`dest_.pw`) — destination-vault password migrates.
- [ ] Verify all four handle Backspace without UTF-8 corruption and accept paste; copy/cut are no-ops.

**7. Migrate the fourteen ordinary text fields**
- [ ] `src/ui/gallery_grid.h:243` (`naming_.buf`, handler `src/ui/gallery_grid.cpp:613`) — new-gallery name prompt migrates to `TextInputModel`.
- [ ] `src/ui/search_overlay.h:54` / `src/ui/search_overlay.cpp` — `query_` migrates; routes events through `handle_text_input_event()`.
- [ ] `src/ui/rename_dialog.h:36` / `src/ui/rename_dialog.cpp` — `buf_` migrates.
- [ ] `src/ui/advanced_search_screen.h:55–58` — `edit_.name`, `edit_.include`, `edit_.exclude`, `edit_.group` each migrate.
- [ ] `src/ui/saved_search_panel.h:94` — `save_buf_` migrates. **Note:** this field has no Backspace handler at all today — the migration fixes an existing defect, so it needs a regression test, not just a port.
- [ ] `src/ui/tag_editor.h:80` — `new_tag_buf_` migrates; the Phase 29 autosuggest logic stays layered on top.
- [ ] `src/ui/tag_overview.h:57` (`filter_`, handler `src/ui/tag_overview.cpp:240`) migrates; filter semantics unchanged.
- [ ] `src/ui/tag_overview.h:62` (`prompt_buf_`, handler `src/ui/tag_overview.cpp:222`) — Phase 51 description prompt migrates.
- [ ] `src/ui/transfer_dialog.h:104` (`name_buf_`, handler `src/ui/transfer_dialog.cpp:143`) — target-gallery naming prompt migrates.
- [ ] `src/ui/gallery_picker.h:59` — `GalleryPickerModel::filter_` migrates; filtering logic stays layered. **One model, two drivers** — `src/ui/transfer_dialog.cpp:187` and `src/ui/combine_dialog.cpp:76`. Re-test through both `M` (transfer) and `Shift+M` (combine); migrating via transfer alone is how `combine_dialog` gets left behind.
- [ ] `src/ui/settings_model.h:28` (`prompt_buf`, handler `src/ui/settings_overlay.cpp`) — category-name prompt migrates.
- [ ] **Do not migrate `src/app/app.cpp:308`** — it matches a `SDL_EVENT_TEXT_INPUT` grep but is not a field; it lumps text input in with mouse events to poke the idle auto-lock timer.
- [ ] All fourteen accept Backspace without UTF-8 corruption, arrow keys, Home/End, selection, copy/cut and paste.

**8. Cross-cutting**
- [ ] Run `scripts/gen.sh` after adding the five new `.cpp` files (`text_input_model`, `text_field_view`, `secure_text_input`, `clipboard`, `text_input_event`) so `compile_commands.json` stays accurate.
- [ ] `scripts/test.sh` all green; `scripts/test.sh --asan` no leaks/UB (this phase touches secure memory, so ASAN is a gate).
- [ ] Update ROADMAP.md index with one row for Phase 54.
- [ ] Update Serena memories: `mem:module/ui` (new modules, retirement of `SecureTextField`); `mem:ui_spec` (caret, selection, paste now available everywhere).

### Acceptance criterion

All eighteen text fields (the four secure plus the fourteen ordinary) accept caret movement via
Left/Right/Home/End and word jumps (`Ctrl+Left/Right`), selection with `Shift+...`, and
deletion by character (Backspace/Delete) or selection. Backspace respects UTF-8 boundaries
and does not corrupt multi-byte characters. Paste works from the OS clipboard on all fields;
copy (`Ctrl+C`) and cut (`Ctrl+X`) work on ordinary fields but are silent no-ops on secure
fields. `Ctrl+A` in a focused text field selects all and is consumed before screen shortcuts.
The old append-only `SecureTextField` is fully retired, and no field is left behind.
`scripts/test.sh` and `scripts/test.sh --asan` both pass.

**Status:** ⬜ Not started
