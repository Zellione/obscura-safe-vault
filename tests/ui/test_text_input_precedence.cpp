#include "test_framework.h"

#include <string>

#include <SDL3/SDL.h>

#include "ui/clipboard.h"
#include "ui/secure_text_input.h"
#include "ui/text_input_event.h"
#include "ui/text_input_model.h"

namespace {

SDL_Event key_event(SDL_Keycode k, SDL_Keymod mod = SDL_KMOD_NONE)
{
    SDL_Event e{};
    e.type    = SDL_EVENT_KEY_DOWN;
    e.key.key = k;
    e.key.mod = mod;
    return e;
}

SDL_Event text_event(const char* s)
{
    SDL_Event e{};
    e.type      = SDL_EVENT_TEXT_INPUT;
    e.text.text = s;
    return e;
}

// Stands in for a host screen's own shortcut handler (e.g. GalleryGrid's
// Phase 53 Ctrl+A select-all-tiles). Records what reached it.
struct MockScreen {
    int  ctrl_a_calls = 0;
    int  other_calls  = 0;

    void handle(const SDL_Event& e)
    {
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_A &&
            (e.key.mod & SDL_KMOD_CTRL) != 0) {
            ++ctrl_a_calls;
            return;
        }
        ++other_calls;
    }
};

// The dispatch order every host must implement: the focused field first, the
// screen only if the field did not consume the event.
void dispatch(ui::ITextInput& field, MockScreen& screen, const SDL_Event& e)
{
    if (ui::handle_text_input_event(field, e)) return;
    screen.handle(e);
}

class NullClipboard final : public ui::ClipboardBackend {
public:
    char* get_text() override { return nullptr; }
    void  release_text(char* /*p*/) override {}
    bool  set_text(std::string_view s) override { last = std::string(s); return true; }
    std::string last;
};

struct InstalledClipboard {
    explicit InstalledClipboard(NullClipboard& c) { ui::set_clipboard_backend(&c); }
    ~InstalledClipboard() { ui::set_clipboard_backend(nullptr); }
    InstalledClipboard(const InstalledClipboard&)            = delete;
    InstalledClipboard& operator=(const InstalledClipboard&) = delete;
    InstalledClipboard(InstalledClipboard&&)                 = delete;
    InstalledClipboard& operator=(InstalledClipboard&&)      = delete;
};

} // namespace

// --- The precedence rule ---------------------------------------------------

TEST(tip_ctrl_a_in_a_focused_field_never_reaches_the_screen)
{
    ui::TextInputModel field;
    field.set_text("gallery name");
    MockScreen screen;

    dispatch(field, screen, key_event(SDLK_A, SDL_KMOD_LCTRL));

    CHECK_EQ(screen.ctrl_a_calls, 0);      // the grid's select-all did NOT fire
    CHECK(field.has_selection());
    CHECK_EQ(field.sel_begin(), size_t{0});
    CHECK_EQ(field.sel_end(), field.size());
}

TEST(tip_clipboard_shortcuts_are_consumed_before_screen_shortcuts)
{
    NullClipboard clip;
    const InstalledClipboard guard(clip);

    ui::TextInputModel field;
    field.set_text("abc");
    MockScreen screen;

    dispatch(field, screen, key_event(SDLK_C, SDL_KMOD_LCTRL));
    dispatch(field, screen, key_event(SDLK_X, SDL_KMOD_LCTRL));
    dispatch(field, screen, key_event(SDLK_V, SDL_KMOD_LCTRL));

    CHECK_EQ(screen.other_calls, 0);
    CHECK_EQ(screen.ctrl_a_calls, 0);
}

TEST(tip_a_bare_letter_types_instead_of_triggering_a_shortcut)
{
    ui::TextInputModel field;
    MockScreen screen;

    // Without Ctrl, SDLK_A is not ours — SDL delivers the character as a
    // separate TEXT_INPUT event, which IS ours.
    dispatch(field, screen, key_event(SDLK_A));
    CHECK_EQ(screen.other_calls, 1);
    CHECK(field.empty());

    dispatch(field, screen, text_event("a"));
    CHECK_EQ(screen.other_calls, 1);
    CHECK_EQ(field.str(), std::string("a"));
}

TEST(tip_screen_level_keys_are_left_alone)
{
    ui::TextInputModel field;
    field.set_text("abc");
    MockScreen screen;

    // Enter / Esc / Tab / Up / Down commit, cancel, cycle focus and drive
    // autosuggest lists; a field must not swallow any of them.
    for (const SDL_Keycode k : {SDLK_RETURN, SDLK_KP_ENTER, SDLK_ESCAPE, SDLK_TAB,
                                SDLK_UP, SDLK_DOWN, SDLK_PAGEUP, SDLK_PAGEDOWN}) {
        CHECK(!ui::handle_text_input_event(field, key_event(k)));
    }
    // Phase 48's detail-panel scroll keeps working while a field is focused.
    CHECK(!ui::handle_text_input_event(field, key_event(SDLK_UP, SDL_KMOD_LCTRL)));
    CHECK(!ui::handle_text_input_event(field, key_event(SDLK_DOWN, SDL_KMOD_LCTRL)));
    // Plain Insert is not a paste.
    CHECK(!ui::handle_text_input_event(field, key_event(SDLK_INSERT)));
    CHECK_EQ(field.str(), std::string("abc"));
}

// --- Event routing ---------------------------------------------------------

TEST(tip_arrows_home_end_and_their_shift_and_ctrl_variants)
{
    ui::TextInputModel field;
    field.set_text("alpha beta");

    CHECK(ui::handle_text_input_event(field, key_event(SDLK_HOME)));
    CHECK_EQ(field.caret(), size_t{0});
    CHECK(ui::handle_text_input_event(field, key_event(SDLK_RIGHT)));
    CHECK_EQ(field.caret(), size_t{1});
    CHECK(ui::handle_text_input_event(field, key_event(SDLK_RIGHT, SDL_KMOD_LCTRL)));
    CHECK_EQ(field.caret(), size_t{5});
    CHECK(ui::handle_text_input_event(field, key_event(SDLK_END, SDL_KMOD_LSHIFT)));
    CHECK_EQ(field.sel_begin(), size_t{5});
    CHECK_EQ(field.sel_end(), size_t{10});
    CHECK(ui::handle_text_input_event(field, key_event(SDLK_LEFT, SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT)));
    CHECK_EQ(field.sel_begin(), size_t{5});
    CHECK_EQ(field.sel_end(), size_t{6});
}

TEST(tip_backspace_and_delete_route_through_the_handler)
{
    ui::TextInputModel field;
    field.set_text("ab");
    CHECK(ui::handle_text_input_event(field, key_event(SDLK_BACKSPACE)));
    CHECK_EQ(field.str(), std::string("a"));
    CHECK(ui::handle_text_input_event(field, key_event(SDLK_HOME)));
    CHECK(ui::handle_text_input_event(field, key_event(SDLK_DELETE)));
    CHECK(field.empty());
}

TEST(tip_shift_insert_pastes_but_plain_insert_does_not)
{
    NullClipboard clip;
    const InstalledClipboard guard(clip);

    ui::TextInputModel field;
    CHECK(!ui::handle_text_input_event(field, key_event(SDLK_INSERT)));
    CHECK(ui::handle_text_input_event(field, key_event(SDLK_INSERT, SDL_KMOD_LSHIFT)));
}

// --- Secure fields ---------------------------------------------------------

TEST(tip_secure_fields_consume_copy_and_cut_as_no_ops)
{
    NullClipboard clip;
    clip.last = "untouched";
    const InstalledClipboard guard(clip);

    ui::SecureTextInput pw;
    pw.insert("hunter2");
    MockScreen screen;

    dispatch(pw, screen, key_event(SDLK_A, SDL_KMOD_LCTRL));
    CHECK(pw.has_selection());
    CHECK_EQ(screen.ctrl_a_calls, 0);

    // Consumed (the screen never sees them) but nothing is copied and, for cut,
    // nothing is deleted either.
    dispatch(pw, screen, key_event(SDLK_C, SDL_KMOD_LCTRL));
    dispatch(pw, screen, key_event(SDLK_X, SDL_KMOD_LCTRL));
    CHECK_EQ(screen.other_calls, 0);
    CHECK_EQ(clip.last, std::string("untouched"));
    CHECK_EQ(pw.size(), size_t{7});
}

TEST(tip_secure_fields_accept_typed_text_and_backspace)
{
    ui::SecureTextInput pw;
    CHECK(ui::handle_text_input_event(pw, text_event("p\xC3\xA4ss")));   // "päss"
    CHECK_EQ(pw.size(), size_t{5});
    CHECK(ui::handle_text_input_event(pw, key_event(SDLK_HOME)));
    CHECK(ui::handle_text_input_event(pw, key_event(SDLK_RIGHT)));
    CHECK(ui::handle_text_input_event(pw, key_event(SDLK_DELETE)));      // drop 'ä'
    CHECK(pw.text_view() == std::string_view("pss"));
}
