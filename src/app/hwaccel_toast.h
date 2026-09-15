#pragma once

// Phase 102: pure visibility predicate for the brief on-screen toast that
// confirms a hwaccel runtime-override toggle (Ctrl+Shift+H, Ctrl+Shift+F,
// or F2 Playback cycle). The toast renders only while the elapsed timer is
// below HWACCEL_TOAST_SECS AND there is text to show (empty text = no
// toast at all, so an app that never sees a toggle never shows one).
// Pure / SDL-free, mirrors should_show_badge (app/keep_unlocked_badge.h).
namespace app {

[[nodiscard]] constexpr bool should_show_hwaccel_toast(
    const char* text, double seconds_since_toggle, double window_secs) noexcept
{
    return text != nullptr && text[0] != '\0' && seconds_since_toggle < window_secs;
}

} // namespace app
