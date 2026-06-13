#include "ui/scroll_model.h"

#include <algorithm>

namespace ui {

ScrollModel::ScrollModel(const std::vector<float>& scaled_heights, float viewport_h)
    : vh_(viewport_h)
{
    tops_.reserve(scaled_heights.size() + 1);
    tops_.push_back(0.0f);
    for (float h : scaled_heights)
        tops_.push_back(tops_.back() + std::max(0.0f, h));
}

int ScrollModel::count() const noexcept
{
    return static_cast<int>(tops_.size()) - 1;
}

float ScrollModel::total_height() const noexcept
{
    return tops_.back();
}

float ScrollModel::max_scroll() const noexcept
{
    return std::max(0.0f, total_height() - vh_);
}

float ScrollModel::clamp_scroll(float scroll_y) const noexcept
{
    return std::clamp(scroll_y, 0.0f, max_scroll());
}

float ScrollModel::image_top(int index) const noexcept
{
    if (index <= 0)            return 0.0f;
    if (index >= count())      return total_height();
    return tops_[static_cast<size_t>(index)];
}

int ScrollModel::active_index(float scroll_y) const noexcept
{
    const int n = count();
    if (n <= 0) return 0;
    const float center = scroll_y + vh_ * 0.5f;
    for (int i = 0; i < n; ++i)
        if (center < tops_[static_cast<size_t>(i) + 1]) return i;
    return n - 1;
}

std::pair<int, int> ScrollModel::visible_range(float scroll_y) const noexcept
{
    const int n = count();
    if (n <= 0) return {0, -1};
    const float top = scroll_y;
    const float bot = scroll_y + vh_;

    int first = n - 1;
    for (int i = 0; i < n; ++i)
        if (tops_[static_cast<size_t>(i) + 1] > top) { first = i; break; }

    int last = 0;
    for (int i = n - 1; i >= 0; --i)
        if (tops_[static_cast<size_t>(i)] < bot) { last = i; break; }

    if (last < first) last = first;
    return {first, last};
}

} // namespace ui
