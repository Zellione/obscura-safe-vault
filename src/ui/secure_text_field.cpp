#include "ui/secure_text_field.h"

namespace ui {

SecureTextField::SecureTextField(size_t capacity) : buf_(capacity)
{
    cap_ = buf_.size();   // 0 if the secure allocation failed
}

void SecureTextField::push_utf8(std::string_view text) noexcept
{
    for (char c : text) {
        if (len_ >= cap_) break;
        buf_.data()[len_++] = static_cast<uint8_t>(c);
    }
}

void SecureTextField::backspace() noexcept
{
    if (len_ == 0) return;
    --len_;
    buf_.data()[len_] = 0;
}

void SecureTextField::clear() noexcept
{
    buf_.wipe();
    len_ = 0;
}

std::span<const uint8_t> SecureTextField::bytes() const noexcept
{
    return {buf_.data(), len_};
}

} // namespace ui
