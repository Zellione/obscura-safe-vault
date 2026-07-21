#include "ui/chrome_layout.h"

#include <algorithm>

namespace ui {

ChromeBands split_chrome(const SDL_FRect& area, float header_h, float footer_h) noexcept
{
    const float avail = std::max(0.0f, area.h);
    float head = std::max(0.0f, header_h);
    float foot = std::max(0.0f, footer_h);

    // Degenerate window: keep both bands inside `area` (shrunk in proportion)
    // rather than letting them overlap each other or drive the content height
    // negative. At this size nothing useful is visible anyway; the point is
    // only that the rects stay well-formed.
    if (const float want = head + foot; want > avail) {
        const float scale = want > 0.0f ? avail / want : 0.0f;
        head *= scale;
        foot *= scale;
    }

    return {
        .header  = {area.x, area.y, area.w, head},
        .content = {area.x, area.y + head, area.w, std::max(0.0f, avail - head - foot)},
        .footer  = {area.x, area.y + avail - foot, area.w, foot},
    };
}

} // namespace ui
