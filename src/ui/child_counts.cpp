#include "ui/child_counts.h"

#include <algorithm>
#include <format>

#include "vault/index.h"

namespace ui {

SubtreeCounts direct_child_counts(const vault::IndexNode& node)
{
    SubtreeCounts c;
    for (const auto& ch : node.children) {
        if (ch.is_gallery())    { ++c.galleries; }
        else if (ch.is_video()) { ++c.videos; }
        else if (ch.is_image()) { ++c.images; }
    }
    return c;
}

std::string format_tile_counts(const SubtreeCounts& c)
{
    const int items = c.images + c.videos;
    const bool has_g = c.galleries > 0;
    const bool has_i = items > 0;
    if (!has_g && !has_i) { return "empty"; }

    const std::string g = std::format("{} {}", c.galleries,
                                      c.galleries == 1 ? "gallery" : "galleries");
    const std::string i = std::format("{} {}", items, items == 1 ? "item" : "items");
    if (has_g && has_i) { return g + " · " + i; }
    return has_g ? g : i;
}

bool any_tile_counts_to_show(std::span<const vault::IndexNode* const> children)
{
    return std::ranges::any_of(children,
        [](const vault::IndexNode* n) { return n != nullptr && n->is_gallery(); });
}

} // namespace ui
