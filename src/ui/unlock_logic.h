#pragma once

#include <cstdint>
#include <span>

namespace ui {

enum class SubmitAction { None, Unlock, Create };

// action == None  =>  error != nullptr (a user-facing message).
struct SubmitDecision {
    SubmitAction action = SubmitAction::None;
    const char*  error  = nullptr;
};

// Validate an unlock/create submission. Pure; performs no crypto or I/O.
[[nodiscard]] SubmitDecision decide_submit(bool                     create_mode,
                                           std::span<const uint8_t> password,
                                           std::span<const uint8_t> confirm) noexcept;

} // namespace ui
