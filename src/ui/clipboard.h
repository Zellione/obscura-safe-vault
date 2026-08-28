#pragma once

// Clipboard access for text fields, behind a seam so the paste/copy paths are
// unit-testable without SDL or a real desktop clipboard.
//
// Secure fields (ui::SecureTextInput) paste straight from the backend's buffer
// into mlock'd storage — no intermediate std::string — and the backend's buffer
// is crypto_wipe'd before it is released. Copy and cut OUT of a secure field are
// refused; see ui/secure_text_input.h for why.
//
// Accepted residual exposure: the OS clipboard itself still holds the plaintext
// after a paste. That is the same exposure the Phase 45 copy-password action
// already accepts and is outside the app's control (AGENTS.md local-attacker
// threat model). Wiping our own copy avoids a preventable SECOND exposure inside
// the process.
//
// Orthogonal to ui/clipboard_secret.h, which owns the Phase 45 auto-clear timer.

#include <string_view>

namespace ui {

class ITextInput;

// The seam. `get_text` returns a NUL-terminated buffer owned by the backend (or
// nullptr); the caller must hand it back to `release_text`, which wipes it.
class ClipboardBackend {
public:
    ClipboardBackend()                                   = default;
    virtual ~ClipboardBackend()                          = default;
    ClipboardBackend(const ClipboardBackend&)            = delete;
    ClipboardBackend& operator=(const ClipboardBackend&) = delete;
    ClipboardBackend(ClipboardBackend&&)                 = delete;
    ClipboardBackend& operator=(ClipboardBackend&&)      = delete;

    [[nodiscard]] virtual char* get_text() = 0;
    virtual void release_text(char* p)     = 0;
    virtual bool set_text(std::string_view s) = 0;
};

// The live backend (SDL-backed by default).
[[nodiscard]] ClipboardBackend& clipboard_backend() noexcept;

// Install a test backend; pass nullptr to restore the SDL one.
void set_clipboard_backend(ClipboardBackend* backend) noexcept;

// Read the clipboard and insert it at `field`'s caret, replacing any selection.
// Returns false when the clipboard is empty or unreadable.
bool paste_from_clipboard(ITextInput& field);

// Put `field`'s selection on the clipboard, subject to the Phase 92 gate: Allow
// writes immediately (true); Warn parks the bytes for the App's default-cancel
// confirm and returns false (nothing written yet); Disable wipes the selection
// and returns false. Always a no-op returning false for a secure field.
bool copy_selection_to_clipboard(const ITextInput& field);

// Copy, then delete the selection. Same gate as copy; the selection is deleted
// only when the clipboard was actually written, so under Warn the text stays put
// until the confirm lands and under Disable nothing is touched. No-op returning
// false for a secure field, so Ctrl+X cannot destroy a password it was not
// allowed to copy.
bool cut_selection_to_clipboard(ITextInput& field);

} // namespace ui
