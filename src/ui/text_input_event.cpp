#include "ui/text_input_event.h"

#include "ui/clipboard.h"
#include "ui/text_input_model.h"

namespace ui {

namespace {

bool handle_key(ITextInput& field, const SDL_KeyboardEvent& key)
{
    const bool ctrl  = (key.mod & SDL_KMOD_CTRL) != 0;
    const bool shift = (key.mod & SDL_KMOD_SHIFT) != 0;

    // Ctrl+Up / Ctrl+Down belong to the detail panel, not to the field.
    if (ctrl && (key.key == SDLK_UP || key.key == SDLK_DOWN)) return false;

    switch (key.key) {
        case SDLK_LEFT:      field.move_left(ctrl, shift);  return true;
        case SDLK_RIGHT:     field.move_right(ctrl, shift); return true;
        case SDLK_HOME:      field.move_home(shift);        return true;
        case SDLK_END:       field.move_end(shift);         return true;
        case SDLK_BACKSPACE: field.backspace();             return true;
        case SDLK_DELETE:    field.del();                   return true;
        case SDLK_INSERT:
            // Shift+Insert is the X11-era paste; plain Insert is not ours.
            if (!shift) return false;
            (void)paste_from_clipboard(field);
            return true;
        case SDLK_A:
            if (!ctrl) return false;
            field.select_all();
            return true;
        case SDLK_C:
            if (!ctrl) return false;
            // Consumed even for a secure field: copy_selection_to_clipboard()
            // refuses, and swallowing the key stops it reaching a screen
            // shortcut behind the focused field.
            (void)copy_selection_to_clipboard(field);
            return true;
        case SDLK_X:
            if (!ctrl) return false;
            (void)cut_selection_to_clipboard(field);
            return true;
        case SDLK_V:
            if (!ctrl) return false;
            (void)paste_from_clipboard(field);
            return true;
        default:
            return false;
    }
}

} // namespace

bool handle_text_input_event(ITextInput& field, const SDL_Event& e)
{
    if (e.type == SDL_EVENT_TEXT_INPUT) {
        field.insert(e.text.text);
        return true;
    }
    if (e.type != SDL_EVENT_KEY_DOWN) return false;
    return handle_key(field, e.key);
}

} // namespace ui
