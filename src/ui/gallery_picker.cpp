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
    multi_         = false;
    checked_.clear();
    rebuild_filtered();
}

void GalleryPickerModel::set_pinned_suffix(std::string item)
{
    pinned_suffix_ = std::move(item);
    rebuild_filtered();
}

void GalleryPickerModel::filter_append(std::string_view utf8)
{
    filter_.insert(utf8);
    rebuild_filtered();
}

void GalleryPickerModel::filter_backspace()
{
    filter_.backspace();
    rebuild_filtered();
}

void GalleryPickerModel::filter_clear()
{
    filter_.clear();
    rebuild_filtered();
}

void GalleryPickerModel::rebuild_filtered()
{
    const auto tokens = tokenize(filter_.view());
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

void GalleryPickerModel::toggle_checked()
{
    if (!multi_ || filtered_.empty() || selected_ < 0 ||
        selected_ >= static_cast<int>(filtered_.size()))
        return;

    const std::string& item = filtered_[selected_];

    // Ignore the pinned suffix row
    if (!pinned_suffix_.empty() && item == pinned_suffix_) return;

    // Toggle: erase if present, push if absent
    auto it = std::ranges::find(checked_, item);
    if (it != checked_.end())
        checked_.erase(it);
    else
        checked_.push_back(item);
}

bool GalleryPickerModel::is_checked(std::string_view item) const
{
    return std::ranges::any_of(checked_, [item](const auto& c) { return c == item; });
}

std::vector<std::string> GalleryPickerModel::checked() const
{
    // Return checked items in items_ order
    std::vector<std::string> result;
    for (const auto& item : items_)
        if (is_checked(item)) result.push_back(item);
    return result;
}

// Remove every path that is a strict descendant of another path in the list.
std::vector<std::string> drop_descendant_paths(std::vector<std::string> paths)
{
    // Sort the paths
    std::ranges::sort(paths);

    // Keep only paths that are not descendants of any previously-kept path
    std::vector<std::string> kept;
    for (const auto& path : paths) {
        bool is_descendant = false;
        for (const auto& kept_path : kept) {
            // Check if path is a descendant of kept_path
            // path is descendant if it starts with kept_path + "/"
            if (path.starts_with(kept_path + "/")) {
                is_descendant = true;
                break;
            }
        }
        if (!is_descendant) kept.push_back(path);
    }
    return kept;
}

} // namespace ui
