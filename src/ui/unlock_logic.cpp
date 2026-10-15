#include "ui/unlock_logic.h"

#include <algorithm>
#include <format>
#include "ui/text_metrics.h"
#include "vault/legacy_converter.h"

namespace ui {

ConversionFormLayout conversion_form_layout(float window_width, float font_px,
                                            float scroll) noexcept
{
    return {std::clamp(window_width - 120, 1.0f, 1100.0f),
            std::max(44.0f, line_pitch(font_px) + 8.0f), 24 - scroll};
}

SDL_FRect ConversionFormLayout::password_field(bool confirm) const noexcept
{
    const float field_width = std::max(1.0f, (width - 24) / 2);
    return {confirm ? 84 + field_width : 60, row(7), field_width, step - 8};
}

std::string conversion_error_message(const vault::LegacyConversionReport& report)
{
    using enum vault::LegacyConversionStatus;
    switch (report.status) {
    case Ok:
        return {};
    case SourceLocked:
        return "Source is locked. Unlock the original vault again.";
    case SourceNotLegacy:
        return "Source already uses the new format; conversion is unnecessary.";
    case InvalidDestination:
        return "Invalid destination. Choose a folder separate from the source.";
    case DestinationExists:
        return "Destination already exists or cannot be opened for resume. Choose another folder.";
    case ResumeMismatch:
        return "Destination is not an incomplete copy of this source. Choose another folder.";
    case AuthenticationFailed:
        return "Resume failed: use the destination password and keyfile from the first attempt.";
    case SourceCorrupt:
        return "Cannot read or authenticate source media. Keep the original and check your backup.";
    case IoError:
        return "Conversion I/O failed. Check destination permissions, free space and drive "
               "connection.";
    case Cancelled:
        return "Cancelled. Retry with the same destination password and keyfile to resume.";
    case VerificationFailed:
        return "Converted copy failed verification. Keep the original; do not use the copy yet.";
    case InsufficientSpace:
        if (report.required_free_bytes == 0)
            return "Cannot estimate required space. Check that the source is accessible.";
        return std::format(
            "Destination needs at least {} MiB free. Check available space and folder access.",
            report.required_free_bytes / (1024 * 1024) +
                (report.required_free_bytes % (1024 * 1024) != 0 ? 1U : 0U));
    }
    return "Unknown conversion error. Keep the original vault.";
}

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
