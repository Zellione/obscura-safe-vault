#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "crypto/secure_mem.h"

namespace ui {

// A text entry field whose bytes live in a single fixed-capacity mlock'd buffer
// (never an unlocked std::string), wiped on clear/destroy. Capacity is allocated
// once so growth never leaves a plaintext copy behind (CLAUDE.md invariant #2).
class SecureTextField {
public:
    explicit SecureTextField(size_t capacity = 512);

    void push_utf8(std::string_view text) noexcept;  // append, clamped at capacity
    void backspace() noexcept;                        // drop the last byte
    void clear() noexcept;                            // wipe + length = 0

    [[nodiscard]] size_t length() const noexcept { return len_; }
    [[nodiscard]] bool   empty()  const noexcept { return len_ == 0; }
    [[nodiscard]] std::span<const uint8_t> bytes() const noexcept;

private:
    crypto::SecureBytes buf_;
    size_t              cap_ = 0;
    size_t              len_ = 0;
};

} // namespace ui
