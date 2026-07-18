#pragma once

#include <optional>
#include <string>

namespace ui {

// Decide whether to clear the OS clipboard now that its auto-clear window
// has elapsed (Phase 45 Part 3): only if it still holds exactly what this
// app last put there — never a blind clear, so a different value the user
// copied elsewhere in the meantime is left alone. `current` is what
// SDL_GetClipboardText() returned (nullopt if the query itself failed).
// Pure — no SDL, no timing; the caller owns the timer and calls this only
// once the auto-clear window has elapsed.
[[nodiscard]] inline bool should_clear_clipboard(const std::optional<std::string>& current,
                                                  const std::string& last_set) noexcept
{
    return !last_set.empty() && current.has_value() && *current == last_set;
}

} // namespace ui
