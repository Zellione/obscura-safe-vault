#include "ui/selectable.h"

namespace ui {

bool is_selectable(const vault::IndexNode& node) noexcept
{
    return node.is_image() || node.is_video() || node.is_gallery();
}

std::vector<int> selectable_indices(std::span<const vault::IndexNode* const> children)
{
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(children.size()); ++i) {
        const vault::IndexNode* const child = children[static_cast<size_t>(i)];
        if (child != nullptr && is_selectable(*child)) {
            out.push_back(i);
        }
    }
    return out;
}

} // namespace ui
