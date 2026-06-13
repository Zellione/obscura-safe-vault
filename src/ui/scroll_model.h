#pragma once

#include <utility>
#include <vector>

// Pure model for the image viewer's fill-width continuous scroll mode. Every
// image is scaled so its width equals the viewport width; the model treats the
// whole leaf gallery as one tall column of those scaled heights stacked
// top-to-bottom with no gaps. SDL- and IO-free so it can be unit-tested.
namespace ui {

class ScrollModel {
public:
    // `scaled_heights[i]` = drawn height of image i at the current viewport
    // width. `viewport_h` = visible height. Negative heights are treated as 0.
    ScrollModel(std::vector<float> scaled_heights, float viewport_h);

    [[nodiscard]] int   count() const noexcept;
    [[nodiscard]] float total_height() const noexcept;
    [[nodiscard]] float max_scroll() const noexcept;            // max(0, total - vh)
    [[nodiscard]] float clamp_scroll(float scroll_y) const noexcept;
    [[nodiscard]] float image_top(int index) const noexcept;    // absolute Y of top

    // Index of the image whose vertical span contains the viewport centre
    // (scroll_y + vh/2), clamped to [0, count-1]. Returns 0 when empty.
    [[nodiscard]] int active_index(float scroll_y) const noexcept;

    // Inclusive [first, last] index range whose spans intersect the viewport
    // [scroll_y, scroll_y + vh). Returns {0, -1} (empty) when there are no
    // images.
    [[nodiscard]] std::pair<int, int> visible_range(float scroll_y) const noexcept;

private:
    std::vector<float> tops_;   // prefix sums, size count+1; tops_.back() == total
    float vh_ = 0.0f;
};

} // namespace ui
