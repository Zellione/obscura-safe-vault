#pragma once

// Editable single-line text input: caret, selection, UTF-8-safe editing.
//
// Phase 54 replaced the app's append-only fields (append on SDL_EVENT_TEXT_INPUT,
// `pop_back()` on Backspace) with one shared model. `pop_back()` drops a single
// BYTE, which silently corrupts every multi-byte UTF-8 character; every operation
// here moves by whole characters instead.
//
// Two storage backends share one interface so `handle_text_input_event()` can
// drive either without templates:
//   * TextInputModel  — std::string storage, for ordinary fields.
//   * SecureTextInput — mlock'd crypto::SecureBytes, for password fields
//                       (ui/secure_text_input.h).
// TextInputBase implements ALL caret/selection/word logic in terms of two storage
// primitives, so the two backends cannot drift apart in behaviour.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace ui {

// --- UTF-8 / word helpers (pure, exposed for the two backends and tests) ---

// Byte offset of the character boundary before / after `pos`. Both clamp to
// [0, buf.size()] and always land on a boundary, so a caret can never split a
// multi-byte sequence. A malformed continuation run is treated as one character.
[[nodiscard]] size_t utf8_prev_boundary(std::span<const uint8_t> buf, size_t pos) noexcept;
[[nodiscard]] size_t utf8_next_boundary(std::span<const uint8_t> buf, size_t pos) noexcept;

// Number of characters in `buf` (lead bytes; continuation bytes don't count).
[[nodiscard]] size_t utf8_char_count(std::span<const uint8_t> buf) noexcept;

// Byte offset of the previous / next word boundary relative to `pos`.
// Both skip whitespace first, then the run of the adjacent character's class,
// so they are exact inverses on well-formed text.
[[nodiscard]] size_t word_boundary_left(std::span<const uint8_t> buf, size_t pos) noexcept;
[[nodiscard]] size_t word_boundary_right(std::span<const uint8_t> buf, size_t pos) noexcept;

// Length of the longest prefix of `text` that is acceptable field input: valid
// UTF-8, no C0 control characters and no DEL. Fields are single-line, so a
// pasted newline or tab must not enter the buffer. Returns 0 when the first
// byte is already unacceptable.
[[nodiscard]] size_t acceptable_input_run(std::string_view text) noexcept;

// Byte offset just past the first `run` bytes of `text` skipped forward to the
// next acceptable byte. Used to walk a paste that contains rejected bytes.
[[nodiscard]] size_t skip_unacceptable(std::string_view text, size_t from) noexcept;

// --- The shared interface -------------------------------------------------

// Everything a text field needs, storage-agnostic. Implemented by
// TextInputModel and SecureTextInput via TextInputBase.
class ITextInput {
public:
    ITextInput()                              = default;
    virtual ~ITextInput()                     = default;
    ITextInput(const ITextInput&)             = delete;
    ITextInput& operator=(const ITextInput&)  = delete;
    ITextInput(ITextInput&&)                  = delete;
    ITextInput& operator=(ITextInput&&)       = delete;

    // Insert at the caret, replacing any selection. Unacceptable bytes (see
    // acceptable_input_run) are dropped; the rest is clamped to byte_cap().
    virtual void insert(std::string_view text) = 0;
    virtual void backspace()                   = 0;   // delete the char before the caret
    virtual void del()                         = 0;   // delete the char at the caret
    virtual void move_left(bool by_word, bool extend)  = 0;
    virtual void move_right(bool by_word, bool extend) = 0;
    virtual void move_home(bool extend)        = 0;
    virtual void move_end(bool extend)         = 0;
    virtual void select_all()                  = 0;
    virtual void delete_selection()            = 0;
    virtual void set_text(std::string_view text) = 0;  // caret to end, selection cleared
    virtual void clear()                       = 0;

    [[nodiscard]] virtual std::span<const uint8_t> bytes() const noexcept = 0;
    [[nodiscard]] virtual size_t byte_cap() const noexcept = 0;
    [[nodiscard]] virtual size_t caret() const noexcept    = 0;
    [[nodiscard]] virtual size_t sel_begin() const noexcept = 0;
    [[nodiscard]] virtual size_t sel_end() const noexcept   = 0;

    // Secure fields never surrender plaintext to the clipboard: the shared event
    // handler turns Ctrl+C / Ctrl+X into no-ops when this is true, and
    // selection_text() returns empty so a mis-wired caller cannot leak either.
    [[nodiscard]] virtual bool secure() const noexcept = 0;

    // Monotonic counter bumped on every CONTENT change (never on a bare caret
    // or selection move). Hosts whose field drives a live filter or an
    // autosuggest list compare it across handle_text_input_event() to decide
    // whether to recompute — a size comparison would miss replacing a selection
    // with same-length text.
    [[nodiscard]] virtual uint64_t revision() const noexcept = 0;

    // The selected bytes as a string. Always empty for a secure field.
    [[nodiscard]] virtual std::string selection_text() const = 0;

    [[nodiscard]] size_t size() const noexcept  { return bytes().size(); }
    [[nodiscard]] bool   empty() const noexcept { return bytes().empty(); }
    [[nodiscard]] bool   has_selection() const noexcept { return sel_begin() != sel_end(); }
};

// --- Shared caret / selection logic ---------------------------------------

// All navigation and editing lives here, expressed over two storage primitives
// (`bytes()` and `splice()`), so TextInputModel and SecureTextInput behave
// identically by construction.
class TextInputBase : public ITextInput {
public:
    void insert(std::string_view text) override;
    void backspace() override;
    void del() override;
    void move_left(bool by_word, bool extend) override;
    void move_right(bool by_word, bool extend) override;
    void move_home(bool extend) override;
    void move_end(bool extend) override;
    void select_all() override;
    void delete_selection() override;
    void set_text(std::string_view text) override;

    [[nodiscard]] size_t caret() const noexcept override { return caret_; }
    [[nodiscard]] size_t sel_begin() const noexcept override;
    [[nodiscard]] size_t sel_end() const noexcept override;
    [[nodiscard]] uint64_t revision() const noexcept override { return revision_; }

protected:
    // Replace the bytes in [start, end) with the first `ins.size()` bytes of
    // `ins`. The base guarantees start <= end <= size() and that the result fits
    // within byte_cap(); a secure backend must wipe any bytes the shift vacates.
    virtual void splice(size_t start, size_t end, std::string_view ins) = 0;

    // Caret, anchor and revision after a storage reset (clear(), or a failed
    // allocation). A clear IS a content change, so it bumps the revision.
    void reset_caret() noexcept { caret_ = 0; anchor_ = 0; ++revision_; }

private:
    // The single call path to splice(), so no content change can forget to bump
    // the revision.
    void apply_splice(size_t start, size_t end, std::string_view ins);
    void place_caret(size_t pos, bool extend) noexcept;
    void insert_run(std::string_view run);

    size_t   caret_    = 0;
    size_t   anchor_   = 0;   // equal to caret_ means "no selection"
    uint64_t revision_ = 0;
};

// --- Ordinary (non-secret) field ------------------------------------------

inline constexpr size_t TEXT_INPUT_DEFAULT_CAP = 4096;

class TextInputModel final : public TextInputBase {
public:
    explicit TextInputModel(size_t byte_cap = TEXT_INPUT_DEFAULT_CAP) noexcept : cap_(byte_cap) {}

    void clear() override;

    [[nodiscard]] std::span<const uint8_t> bytes() const noexcept override;
    [[nodiscard]] size_t byte_cap() const noexcept override { return cap_; }
    [[nodiscard]] bool   secure() const noexcept override { return false; }
    [[nodiscard]] std::string selection_text() const override;

    // Direct access for call sites that pass the field straight to the vault.
    [[nodiscard]] const std::string& str() const noexcept { return buf_; }
    [[nodiscard]] std::string_view   view() const noexcept { return buf_; }

protected:
    void splice(size_t start, size_t end, std::string_view ins) override;

private:
    std::string buf_;
    size_t      cap_;
};

// A view over a field's bytes. Safe for an ordinary field; for a SECURE field
// this aliases the mlock'd buffer, so never copy the result into a std::string.
[[nodiscard]] inline std::string_view buffer_text(const ITextInput& f) noexcept
{
    const auto b = f.bytes();
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

} // namespace ui
