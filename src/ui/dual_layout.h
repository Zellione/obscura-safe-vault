#pragma once

#include <SDL3/SDL.h>
#include <span>
#include <string>
#include <string_view>

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

// Why a pane-to-pane transfer cannot proceed (checked BEFORE the modal opens).
enum class DualTransferRefusal { None, SameGallery, IntoOwnSubtree };

// src_gallery/dst_gallery: the two panes' current gallery slash-paths.
// selected_gallery_paths: full paths of galleries in the selection.
[[nodiscard]] DualTransferRefusal dual_transfer_check(
    std::string_view src_gallery, std::string_view dst_gallery,
    std::span<const std::string> selected_gallery_paths) noexcept;

// Copy an SDL event, shifting mouse coordinates into pane-local space
// (subtract pane origin). Non-mouse events pass through unchanged.
[[nodiscard]] SDL_Event translate_event_to_pane(const SDL_Event& e, const SDL_FRect& pane) noexcept;

} // namespace ui
