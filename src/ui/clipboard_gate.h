#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "platform/clipboard_pref.h"

namespace ui {

// Policy outcome for a clipboard-write attempt (Phase 92). Pure mapping, so the
// wording is unit-tested without a platform or SDL: Allow writes immediately,
// Warn asks first (a default-cancel confirm), Disable refuses outright.
enum class ClipboardGateAction { Copy, Confirm, Refuse };

[[nodiscard]] constexpr ClipboardGateAction
clipboard_gate_action(platform::ClipboardMode m) noexcept
{
    using enum ClipboardGateAction;
    using enum platform::ClipboardMode;
    switch (m) {
    case Allow:
        return Copy;
    case Warn:
        return Confirm;
    case Disable:
        return Refuse;
    }
    return Copy;
}

// Process-global clipboard gate (Phase 92). Seeded from platform::ClipboardPref
// at App::init and written back by the F2 settings toggle (saved live — no
// exit-save needed; settings is the only writer). UI-thread only (like the
// active-theme global). Every clipboard write in the app consults it.
[[nodiscard]] platform::ClipboardMode clipboard_gate() noexcept;
void set_clipboard_gate(platform::ClipboardMode m) noexcept;

// --- Warn path: the pending confirmation ------------------------------------
// Under Warn, a copy attempt is parked here instead of writing to the OS
// clipboard. The App renders a default-cancel confirm modal while
// confirm_pending() is true, then calls confirm_clipboard_copy() /
// cancel_clipboard_copy().

// Park a copy payload awaiting confirmation. The bytes are copied into an
// mlock'd, wipe-on-destroy buffer — the payload may be a password. `sensitive`
// marks a copy that the unlock screen arms its auto-clear timer for once it is
// confirmed (Phase 45 behaviour preserved through the gate). A non-empty text
// that cannot be stored (OOM) leaves no pending confirm and returns false.
[[nodiscard]] bool request_clipboard_confirm(std::string text, bool sensitive);

[[nodiscard]] bool clipboard_confirm_pending() noexcept;

// View straight over the mlock'd pending buffer. Like SecureTextInput's
// selection_text(), never copy this into a std::string. Empty unless pending.
[[nodiscard]] std::string_view clipboard_confirm_text() noexcept;

[[nodiscard]] bool clipboard_confirm_sensitive() noexcept;

// Write the pending payload to the OS clipboard via the ui::ClipboardBackend
// seam and clear the pending state. Returns the write's success. The payload is
// retained (wiped on the next request/cancel/reset) so take_confirmed_copy()
// can hand it to the unlock screen, which arms its auto-clear timer and needs
// the exact text for the "only clear if it still matches" check.
[[nodiscard]] bool confirm_clipboard_copy();

// Abandon the pending payload: wipe it and clear state. No write.
void cancel_clipboard_copy() noexcept;

// The most recent confirmed copy (consumed on read), for auto-clear arming.
struct ConfirmedCopy {
    std::string text;  // transient: what the app wrote; consumed within a frame
    bool sensitive;    // a password/passphrase copy
};

[[nodiscard]] std::optional<ConfirmedCopy> take_confirmed_copy() noexcept;

// Clear all gate state: mode back to Allow, no pending confirm, no retained
// confirmed copy. Used by the test suite to keep tests order-independent.
void reset_clipboard_gate() noexcept;

}  // namespace ui