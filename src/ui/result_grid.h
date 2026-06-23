#pragma once

namespace ui {

// Result-panel presentation mode on the advanced-search screen (Phase 20).
// Session-scoped (lives only while the screen is open); defaults to List, the
// original Phase 18 behaviour. The first enumerator is List so a value-
// initialised ResultView{} is List.
enum class ResultView { List, Grid };

// Flip List <-> Grid. The toggle is its own inverse, so it is the whole
// "state machine": there are only two modes and Ctrl+L cycles between them.
[[nodiscard]] ResultView toggle_result_view(ResultView v) noexcept;

// Arrow-key directions for moving the result cursor.
enum class MoveDir { Left, Right, Up, Down };

// Selection-index delta for an arrow key, given the view and the live column
// count. In List view results are one-per-row: Up/Down step ±1, Left/Right do
// nothing. In Grid view Left/Right step ±1 and Up/Down step a whole row (±cols,
// with cols clamped to >= 1 so a degenerate layout can't freeze navigation).
[[nodiscard]] int result_move_delta(ResultView v, MoveDir dir, int cols) noexcept;

// Apply a directional move to `index`, clamping the result into [0, count).
// An empty result set (count <= 0) always yields 0.
[[nodiscard]] int result_move(ResultView v, int index, MoveDir dir,
                              int count, int cols) noexcept;

} // namespace ui
