#pragma once

namespace app {

// Pure decision for whether the Phase 33 "keep unlocked" corner badge should
// be visible this frame (Phase 45 Part 6): shown for a fixed window after the
// `U` toggle leaves auto-lock off, then hidden until the next `U` press —
// never on generic mouse/key activity. Pure/SDL-free, same shape as
// should_auto_lock (app/auto_lock.h).
[[nodiscard]] constexpr bool should_show_badge(bool keep_unlocked, double seconds_since_toggle,
                                                double window_secs) noexcept
{
    return keep_unlocked && seconds_since_toggle < window_secs;
}

} // namespace app
