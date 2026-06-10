#include "ui/unlock_logic.h"

#include <algorithm>

namespace ui {

SubmitDecision decide_submit(bool                     create_mode,
                             std::span<const uint8_t> password,
                             std::span<const uint8_t> confirm) noexcept
{
    if (password.empty())
        return {SubmitAction::None, "Password cannot be empty."};

    if (create_mode) {
        if (!std::ranges::equal(password, confirm))
            return {SubmitAction::None, "Passwords do not match."};
        return {SubmitAction::Create, nullptr};
    }
    return {SubmitAction::Unlock, nullptr};
}

} // namespace ui
