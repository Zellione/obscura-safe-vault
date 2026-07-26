#pragma once

// SecureTextInput — the ITextInput backend for password fields.
//
// Same interface and, via TextInputBase, exactly the same caret/selection
// semantics as TextInputModel, but the bytes live in a single fixed-capacity
// mlock'd buffer instead of a std::string. Capacity is allocated once so growth
// never leaves a plaintext copy behind, every mutation crypto_wipe's the bytes
// the shift vacates, and clear() wipes the whole buffer (CLAUDE.md invariant #2).
//
// Replaced the append-only SecureTextField in Phase 54.
//
// Copy and cut are deliberately unavailable: selection_text() always returns
// empty and handle_text_input_event() suppresses Ctrl+C / Ctrl+X for any field
// whose secure() is true. The Phase 45 "copy password to clipboard" action —
// which arms a visible auto-clear timer — stays the only way plaintext leaves a
// password field. Paste IN is allowed: the OS clipboard is already readable by
// every other process the user runs, so it adds no new exposure.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "crypto/secure_mem.h"
#include "ui/text_input_model.h"

namespace ui {

inline constexpr size_t SECURE_TEXT_INPUT_DEFAULT_CAP = 512;

class SecureTextInput final : public TextInputBase {
public:
    explicit SecureTextInput(size_t capacity = SECURE_TEXT_INPUT_DEFAULT_CAP);

    void clear() override;

    [[nodiscard]] std::span<const uint8_t> bytes() const noexcept override;
    [[nodiscard]] size_t byte_cap() const noexcept override { return cap_; }
    [[nodiscard]] bool   secure() const noexcept override { return true; }

    // Always empty — a secure field never hands its plaintext to a caller that
    // is about to put it on the clipboard. See the header comment.
    [[nodiscard]] std::string selection_text() const override { return {}; }

    // A view straight over the mlock'd bytes, for the few call sites that must
    // read the password (KDF, strength estimate). Never copy it into a
    // std::string; it is not NUL-terminated.
    [[nodiscard]] std::string_view text_view() const noexcept;

protected:
    void splice(size_t start, size_t end, std::string_view ins) override;

private:
    crypto::SecureBytes buf_;
    size_t              cap_ = 0;
    size_t              len_ = 0;
};

} // namespace ui
