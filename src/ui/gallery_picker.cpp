#include "ui/gallery_picker.h"

#include <algorithm>
#include <utility>

#include "ui/search_model.h"

namespace ui {

void GalleryPickerModel::set_items(std::vector<std::string> items)
{
    items_         = std::move(items);
    filter_.clear();
    filter_open_   = false;
    selected_      = 0;
    pinned_suffix_.clear();
    rebuild_filtered();
}

void GalleryPickerModel::set_pinned_suffix(std::string item)
{
    pinned_suffix_ = std::move(item);
    rebuild_filtered();
}

void GalleryPickerModel::filter_append(std::string_view utf8)
{
    filter_ += utf8;
    rebuild_filtered();
}

void GalleryPickerModel::filter_backspace()
{
    if (!filter_.empty()) filter_.pop_back();
    rebuild_filtered();
}

void GalleryPickerModel::filter_clear()
{
    filter_.clear();
    rebuild_filtered();
}

void GalleryPickerModel::rebuild_filtered()
{
    const auto tokens = tokenize(filter_);
    filtered_.clear();
    filtered_.reserve(items_.size() + 1);
    for (const auto& item : items_)
        if (matches(tokens, item, {})) filtered_.push_back(item);

    if (!pinned_suffix_.empty() &&
        std::ranges::find(filtered_, pinned_suffix_) == filtered_.end())
        filtered_.push_back(pinned_suffix_);

    selected_ = filtered_.empty() ? 0
                                  : std::clamp(selected_, 0, static_cast<int>(filtered_.size()) - 1);
}

void GalleryPickerModel::move(int delta) noexcept
{
    if (filtered_.empty()) { selected_ = 0; return; }
    selected_ = std::clamp(selected_ + delta, 0, static_cast<int>(filtered_.size()) - 1);
}

GalleryPickerModel::Geom GalleryPickerModel::geom(int visible_rows) const noexcept
{
    const auto count   = static_cast<int>(filtered_.size());
    const int visible = std::max(1, std::min(visible_rows, count));
    int       first    = 0;
    if (count > visible) first = std::clamp(selected_ - visible / 2, 0, count - visible);
    return {first, visible};
}

} // namespace ui
