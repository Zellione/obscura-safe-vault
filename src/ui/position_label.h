#pragma once

#include <string>

// Pure formatter for gallery position counter: index to "N / Count" display.
// Returns empty string if count is 0, index < 0, or index >= (int)count.
// Index is 0-based input; output is 1-based ("1 / 5" for index=0, count=5).
namespace ui {

[[nodiscard]] std::string position_label(int index, std::size_t count);

} // namespace ui
