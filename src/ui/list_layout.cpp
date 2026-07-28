#include "ui/list_layout.h"

#include <algorithm>
#include <cmath>

namespace ui {

float list_row_y(const ListMetrics& m, int index)
{
    return m.top + static_cast<float>(std::max(0, index)) * (m.row_h + m.gap);
}

int list_visible_rows(const ListMetrics& m, float bottom)
{
    if (m.row_h <= 0.0f) return 0;
    const float span = bottom - m.top;
    if (span < m.row_h) return 0;
    // n rows occupy n*row_h + (n-1)*gap; solve for the largest n that fits.
    const float pitch = m.row_h + m.gap;
    const auto  n     = static_cast<int>(std::floor((span + m.gap) / pitch));
    return std::max(0, n);
}

int list_rows_fit(const ListMetrics& m, float bottom, int count)
{
    return std::clamp(count, 0, list_visible_rows(m, bottom));
}

} // namespace ui
