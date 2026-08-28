#include "ui/clipboard_gate.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "crypto/secure_mem.h"

#include "ui/clipboard.h"
#include "ui/text_input_model.h"

namespace ui {

namespace {

// All gate state is function-local (like the active-theme slot in gfx/theme.cpp
// and media::autoplay_setting) so it stays mutable without namespace-scope
// globals. UI-thread only; no synchronisation needed.
platform::ClipboardMode& mode_slot() noexcept
{
    static platform::ClipboardMode mode = platform::ClipboardMode::Allow;
    return mode;
}

struct Pending {
    crypto::SecureBytes text;  // mlock'd best-effort, wiped on destroy
    bool sensitive = false;
    bool active = false;
    ITextInput* cut_target = nullptr;
    uint64_t cut_revision = 0;
    size_t cut_begin = 0;
    size_t cut_end = 0;
};

Pending& pending_slot() noexcept
{
    static Pending pending;
    return pending;
}

// The last confirmed copy, retained until consumed. A plain std::string: the
// payload has already been handed to the OS clipboard by confirm, so holding a
// transient plaintext copy here adds no exposure the clipboard itself does not
// have, and the consumer (unlock-screen auto-clear) needs the exact bytes to
// compare against SDL_GetClipboardText().
std::optional<ConfirmedCopy>& confirmed_slot() noexcept
{
    static std::optional<ConfirmedCopy> confirmed;
    return confirmed;
}

void wipe_confirmed_copy() noexcept
{
    auto& confirmed = confirmed_slot();
    if (confirmed) crypto_wipe(confirmed->text.data(), confirmed->text.size());
    confirmed.reset();
}

}  // namespace

platform::ClipboardMode clipboard_gate() noexcept
{
    return mode_slot();
}

void set_clipboard_gate(platform::ClipboardMode m) noexcept
{
    mode_slot() = m;
}

bool request_clipboard_confirm(std::string text, bool sensitive, ITextInput* cut_target)
{
    if (text.empty()) return true;  // nothing to copy: not an error, nothing pending

    Pending& p = pending_slot();
    p.active = p.text.assign({reinterpret_cast<const uint8_t*>(text.data()), text.size()});
    crypto_wipe(text.data(), text.size());
    p.sensitive = p.active && sensitive;
    if (p.active && cut_target != nullptr) {
        p.cut_target = cut_target;
        p.cut_revision = cut_target->revision();
        p.cut_begin = cut_target->sel_begin();
        p.cut_end = cut_target->sel_end();
    } else {
        p.cut_target = nullptr;
        p.cut_revision = 0;
        p.cut_begin = 0;
        p.cut_end = 0;
    }
    return p.active;
}

bool clipboard_confirm_pending() noexcept
{
    return pending_slot().active;
}

std::string_view clipboard_confirm_text() noexcept
{
    const Pending& p = pending_slot();
    if (!p.active) return {};
    return {reinterpret_cast<const char*>(p.text.data()), p.text.size()};
}

bool clipboard_confirm_sensitive() noexcept
{
    return pending_slot().sensitive;
}

bool confirm_clipboard_copy()
{
    // `p` is read-only here: the pending slot is reset through the slot, never
    // through this reference.
    const Pending& p = pending_slot();
    if (!p.active) return false;

    const std::string_view text = clipboard_confirm_text();
    const bool ok = clipboard_backend().set_text(text);

    // Retain only a password that actually reached the clipboard. Ordinary
    // copies have no consumer, and retaining them would leave names/tags/search
    // text in a plain process-global std::string indefinitely.
    if (ok) {
        wipe_confirmed_copy();
        if (p.sensitive) confirmed_slot() = ConfirmedCopy{std::string(text), true};
    }

    // The modal blocks edits while pending, but verify the original selection
    // anyway so an unexpected programmatic mutation cannot delete new text.
    if (ok && p.cut_target != nullptr && p.cut_target->revision() == p.cut_revision &&
        p.cut_target->sel_begin() == p.cut_begin && p.cut_target->sel_end() == p.cut_end) {
        p.cut_target->delete_selection();
    }
    pending_slot() = {};
    return ok;
}

void cancel_clipboard_copy() noexcept
{
    pending_slot() = {};
}

std::optional<ConfirmedCopy> take_confirmed_copy() noexcept
{
    std::optional<ConfirmedCopy> out = std::move(confirmed_slot());
    // A moved-from std::string is valid but its contents are unspecified; wipe
    // any storage the implementation did not transfer before clearing the slot.
    wipe_confirmed_copy();
    return out;
}

void reset_clipboard_gate() noexcept
{
    mode_slot() = platform::ClipboardMode::Allow;
    pending_slot() = {};
    wipe_confirmed_copy();
}

}  // namespace ui
