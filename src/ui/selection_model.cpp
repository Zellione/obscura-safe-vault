#include "ui/selection_model.h"

#include <algorithm>

namespace ui {

void SelectionModel::toggle(int i)
{
    if (auto it = items_.find(i); it != items_.end()) {
        items_.erase(it);
        // Deselecting the anchor hands the role back to the previous one; a
        // deselected index must never remain a span endpoint.
        if (i == anchor_) {
            anchor_      = prev_anchor_;
            prev_anchor_ = -1;
        } else if (i == prev_anchor_) {
            prev_anchor_ = -1;
        }
    } else {
        items_.insert(i);
        prev_anchor_ = anchor_;
        anchor_      = i;
    }
    ++revision_;
}

void SelectionModel::clear()
{
    items_.clear();
    anchor_      = -1;
    prev_anchor_ = -1;
    ++revision_;
}

void SelectionModel::select_all(int count)
{
    if (count <= 0) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        items_.insert(i);
    }
    ++revision_;
}

void SelectionModel::select(int i)
{
    items_.insert(i);
    ++revision_;
}

void SelectionModel::select_range(int a, int b)
{
    const auto [lo, hi] = std::minmax(a, b);
    for (int i = lo; i <= hi; ++i) {
        items_.insert(i);
    }
    ++revision_;
}

std::optional<std::pair<int, int>> SelectionModel::range_for(int focus) const
{
    if (anchor_ >= 0 && anchor_ != focus) {
        return std::pair{anchor_, focus};
    }
    if (anchor_ == focus && prev_anchor_ >= 0) {
        return std::pair{prev_anchor_, focus};
    }
    return std::nullopt;
}

bool SelectionModel::all_selected(int count) const
{
    if (count <= 0) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        if (!items_.contains(i)) {
            return false;
        }
    }
    return true;
}

bool SelectionModel::contains(int i) const { return items_.contains(i); }

bool SelectionModel::empty() const { return items_.empty(); }

std::size_t SelectionModel::count() const { return items_.size(); }

std::vector<int> SelectionModel::indices() const
{
    // std::set iterates in ascending order, so this is already sorted.
    return {items_.begin(), items_.end()};
}

} // namespace ui
