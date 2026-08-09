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

float list_clamp_scroll(float scroll, int count, float row_h, float top, float max_h) noexcept
{
    if (count <= 0 || row_h <= 0.0f) return 0.0f;

    const float available_h = max_h - top;
    if (available_h <= 0.0f) return 0.0f;

    // Content height: header line + count rows (each row_h tall).
    const float content_h = (static_cast<float>(count) + 1.0f) * row_h;

    // If content fits entirely, no scroll needed.
    if (content_h <= available_h) return 0.0f;

    // Clamp to [0, content_h - available_h].
    const float max_offset = content_h - available_h;
    return std::clamp(scroll, 0.0f, max_offset);
}

} // namespace ui
