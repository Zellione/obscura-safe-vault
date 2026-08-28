#include "ui/clipboard_gate.h"

#include <cstdint>
#include <utility>

#include "crypto/secure_mem.h"

#include "ui/clipboard.h"

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

}  // namespace

platform::ClipboardMode clipboard_gate() noexcept
{
    return mode_slot();
}

void set_clipboard_gate(platform::ClipboardMode m) noexcept
{
    mode_slot() = m;
}

bool request_clipboard_confirm(std::string text, bool sensitive)
{
    if (text.empty()) return true;  // nothing to copy: not an error, nothing pending

    Pending& p = pending_slot();
    p.sensitive = sensitive;
    p.active = p.text.assign({reinterpret_cast<const uint8_t*>(text.data()), text.size()});
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

    // Retain for take_confirmed_copy() (auto-clear arming), then drop the pending.
    confirmed_slot() = ConfirmedCopy{std::string(text), p.sensitive};
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
    confirmed_slot().reset();
    return out;
}

void reset_clipboard_gate() noexcept
{
    mode_slot() = platform::ClipboardMode::Allow;
    pending_slot() = {};
    confirmed_slot().reset();
}

}  // namespace ui