#include "ui/selection_model.h"

namespace ui {

void SelectionModel::toggle(int i)
{
    if (auto it = items_.find(i); it != items_.end())
        items_.erase(it);
    else
        items_.insert(i);
    ++revision_;
}

void SelectionModel::clear()
{
    items_.clear();
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
