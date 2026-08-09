#include "ui/strip_scroll.h"

#include <algorithm>

namespace ui {

void strip_apply_wheel(StripScrollState& st, float wheel_y, float step, float extent,
                       float content) noexcept
{
    // If content fits entirely in the window, scrolling is a no-op.
    if (content <= extent) {
        st.offset = 0.0f;
        st.manual = false;
        return;
    }

    // Wheel is now controlling scroll.
    st.manual = true;

    // Adjust offset: negative wheel_y scrolls forward (offset increases),
    // positive scrolls backward (offset decreases).
    st.offset -= wheel_y * step;

    // Clamp to [0, content - extent].
    const float max_offset = content - extent;
    st.offset = std::clamp(st.offset, 0.0f, max_offset);
}

void strip_follow_index(StripScrollState& st) noexcept
{
    st.offset = 0.0f;
    st.manual = false;
}

} // namespace ui
