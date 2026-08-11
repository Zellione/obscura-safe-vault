#pragma once

#include <SDL3/SDL.h>

namespace ui {

// Below this window width F3 refuses to enter split view (Phase 77).
inline constexpr float MIN_SPLIT_WIDTH = 900.0f;

// Fixed 50/50 vertical split: left pane, divider strip, right pane. The three
// rects tile {0,0,win_w,win_h} exactly (same invariant style as split_chrome).
struct DualSplit {
    SDL_FRect left{};
    SDL_FRect right{};
    SDL_FRect divider{};
};

[[nodiscard]] DualSplit dual_split(float win_w, float win_h) noexcept;

// Which pane owns window-x coordinate `x`: 0 = left, 1 = right. Divider
// coordinates resolve to the nearest pane (left half -> 0).
[[nodiscard]] int pane_at(const DualSplit& s, float x) noexcept;

} // namespace ui
