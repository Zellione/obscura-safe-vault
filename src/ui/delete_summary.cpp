#include "ui/delete_summary.h"

#include "vault/index.h"

#include <format>
#include <string_view>

namespace ui {

void count_subtree(const vault::IndexNode& node, SubtreeCounts& c)
{
    for (const auto& ch : node.children) {
        if (ch.is_gallery())    { ++c.galleries; count_subtree(ch, c); }
        else if (ch.is_video()) { ++c.videos; }
        else if (ch.is_image()) { ++c.images; }
    }
}

std::string describe_subtree(const SubtreeCounts& c)
{
    std::string out;
    const auto add = [&](int n, std::string_view singular, std::string_view plural) {
        if (n == 0) return;
        if (!out.empty()) out += ", ";
        out += std::format("{} {}", n, n == 1 ? singular : plural);
    };
    add(c.images, "image", "images");
    add(c.videos, "video", "videos");
    add(c.galleries, "sub-gallery", "sub-galleries");
    return out.empty() ? "nothing" : out;
}

}  // namespace ui
