#include "ui/position_label.h"

#include <format>

namespace ui {

std::string position_label(int index, std::size_t count)
{
    // Return empty when count is 0 or index is out of range
    if (count == 0 || index < 0 || index >= static_cast<int>(count)) {
        return "";
    }

    // Format as "N / Count" using 1-based position (index + 1)
    return std::format("{} / {}", index + 1, count);
}

} // namespace ui
