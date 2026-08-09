#pragma once

// Pure scroll-state model for manual wheel scrolling on the image-viewer
// thumbnail strip. The strip normally auto-centers on the current image;
// wheel input hands over to manual scrolling until the image changes.
// Headless / unit-testable.

namespace ui {

// Scroll state: offset when manual, otherwise auto-centered each frame.
struct StripScrollState {
    float offset = 0.0f;   // scroll pixels (only used when manual=true)
    bool  manual = false;  // true = wheel has taken over; false = auto-centering
};

// Total content width/height of `count` thumbnails of side `thumb` separated by `gap`.
[[nodiscard]] constexpr float strip_content_extent(int count, float thumb, float gap) noexcept
{
    if (count <= 0) return 0.0f;
    return static_cast<float>(count) * thumb + static_cast<float>(count - 1) * gap;
}

// Apply wheel motion: adjust offset by `wheel_y * step` and clamp to [0, max_offset].
// If content fits entirely (content <= extent), keep offset at 0 and manual as false.
// Otherwise set manual to true after this wheel event.
void strip_apply_wheel(StripScrollState& st, float wheel_y, float step, float extent,
                       float content) noexcept;

// Re-engage auto-centering: reset offset to 0 and manual to false.
void strip_follow_index(StripScrollState& st) noexcept;

} // namespace ui
