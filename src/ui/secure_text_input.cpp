#include "ui/secure_text_input.h"

#include <cstring>

#include <monocypher.h>

namespace ui {

SecureTextInput::SecureTextInput(size_t capacity) : buf_(capacity)
{
    cap_ = buf_.size();   // 0 if the secure allocation failed
}

std::span<const uint8_t> SecureTextInput::bytes() const noexcept
{
    return {buf_.data(), len_};
}

std::string_view SecureTextInput::text_view() const noexcept
{
    return {reinterpret_cast<const char*>(buf_.data()), len_};
}

void SecureTextInput::clear()
{
    buf_.wipe();
    len_ = 0;
    reset_caret();
}

void SecureTextInput::splice(size_t start, size_t end, std::string_view ins)
{
    if (cap_ == 0) return;   // secure allocation failed; stay empty

    uint8_t*     p       = buf_.data();
    const size_t old_len = len_;
    const size_t tail    = old_len - end;
    const size_t new_len = old_len - (end - start) + ins.size();

    // Shift the tail into its final place first (memmove handles the overlap in
    // both directions), then lay the insert down in the gap that opens up.
    if (tail > 0) std::memmove(p + start + ins.size(), p + end, tail);
    if (!ins.empty()) std::memcpy(p + start, ins.data(), ins.size());

    // Invariant #2: bytes the shift vacated still hold plaintext. Wipe them
    // rather than merely shortening the length.
    if (new_len < old_len) crypto_wipe(p + new_len, old_len - new_len);

    len_ = new_len;
}

} // namespace ui
