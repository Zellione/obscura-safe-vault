#pragma once

#include <SDL3/SDL.h>
#include <cstdint>
#include <span>
#include <string>

namespace vault {
struct LegacyConversionReport;
enum class ConversionPhase : uint8_t;
}

namespace ui {

struct ConversionProgressText {
    std::string_view title;
    std::string count_line;
    int done = 0;
    int total = 0;
};
[[nodiscard]] ConversionProgressText conversion_progress_text(vault::ConversionPhase phase,
                                                              int done, int total, uint64_t bytes);

struct ConversionFormLayout {
    float width;
    float step;
    float top;
    [[nodiscard]] float row(float index) const noexcept
    {
        return top + index * step;
    }
    [[nodiscard]] SDL_FRect password_field(bool confirm) const noexcept;
};
[[nodiscard]] ConversionFormLayout conversion_form_layout(float window_width, float font_px,
                                                          float scroll = 0) noexcept;

[[nodiscard]] std::string conversion_error_message(const vault::LegacyConversionReport& report);

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
