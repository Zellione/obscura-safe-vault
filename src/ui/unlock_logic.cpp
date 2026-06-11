#include "ui/unlock_logic.h"

#include <algorithm>

namespace ui {

SubmitDecision decide_submit(bool                     create_mode,
                             std::span<const uint8_t> password,
                             std::span<const uint8_t> confirm) noexcept
{
    using enum SubmitAction;
    if (password.empty())
        return {None, "Password cannot be empty."};

    if (create_mode) {
        if (!std::ranges::equal(password, confirm))
            return {None, "Passwords do not match."};
        return {Create, nullptr};
    }
    return {Unlock, nullptr};
}

} // namespace ui
