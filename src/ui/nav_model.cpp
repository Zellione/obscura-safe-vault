#include "ui/nav_model.h"

namespace ui {

std::vector<std::string> split_path(std::string_view path)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= path.size()) {
        size_t slash = path.find('/', start);
        if (slash == std::string_view::npos) {
            if (start < path.size()) out.emplace_back(path.substr(start));
            break;
        }
        if (slash > start) out.emplace_back(path.substr(start, slash - start));
        start = slash + 1;
    }
    return out;
}

std::string join_path(std::span<const std::string> segs)
{
    std::string out;
    for (size_t i = 0; i < segs.size(); ++i) {
        if (i) out += '/';
        out += segs[i];
    }
    return out;
}

void NavModel::enter(std::string segment)
{
    segments_.push_back(std::move(segment));
    selected_ = 0;
    count_    = 0;
}

bool NavModel::up() noexcept
{
    if (segments_.empty()) return false;
    segments_.pop_back();
    selected_ = 0;
    count_    = 0;
    return true;
}

std::string NavModel::path() const { return join_path(segments_); }

void NavModel::set_count(int n) noexcept { count_ = n < 0 ? 0 : n; clamp(); }
void NavModel::move(int delta) noexcept  { selected_ += delta; clamp(); }
void NavModel::select(int index) noexcept { selected_ = index; clamp(); }

void NavModel::clamp() noexcept
{
    if (count_ <= 0)            { selected_ = 0; return; }
    if (selected_ < 0)          selected_ = 0;
    if (selected_ > count_ - 1) selected_ = count_ - 1;
}

} // namespace ui
