#pragma once

#include "ui/gallery_view.h"

namespace ui {

// Result-panel presentation mode on the advanced-search screen (Phase 20,
// unified with the gallery grid in Phase 93). The cursor-movement helpers below
// take a ui::GalleryView: List renders one result per row, any grid density
// renders a thumbnail grid with rows of `cols`.
//
// The first enumerator in GalleryView is List, so a value-initialised
// GalleryView{} is List (the original Phase 18 behaviour).

// Arrow-key directions for moving the result cursor.
enum class MoveDir { Left, Right, Up, Down };

// Selection-index delta for an arrow key, given the view and the live column
// count. In List view results are one-per-row: Up/Down step ±1, Left/Right do
// nothing. In a grid view Left/Right step ±1 and Up/Down step a whole row
// (±cols, with cols clamped to >= 1 so a degenerate layout can't freeze
// navigation).
[[nodiscard]] int result_move_delta(GalleryView v, MoveDir dir, int cols) noexcept;

// Apply a directional move to `index`, clamping the result into [0, count).
// An empty result set (count <= 0) always yields 0.
[[nodiscard]] int result_move(GalleryView v, int index, MoveDir dir, int count, int cols) noexcept;

}  // namespace ui