#include "ui/listing_remap.h"

#include <algorithm>

namespace ui {

namespace {
int index_of(std::span<const std::string> hay, const std::string& needle)
{
    const auto it = std::ranges::find(hay, needle);
    return it == hay.end() ? -1 : static_cast<int>(std::distance(hay.begin(), it));
}

int clamped(int i, std::span<const std::string> names)
{
    if (names.empty()) return 0;
    return std::clamp(i, 0, static_cast<int>(names.size()) - 1);
}
} // namespace

ListingRemap remap_listing(std::span<const std::string> old_names,
                           std::span<const std::string> new_names,
                           int                          old_selected,
                           std::span<const int>         old_multi)
{
    ListingRemap out;
    out.unchanged = std::ranges::equal(old_names, new_names);

    if (old_selected >= 0 && old_selected < static_cast<int>(old_names.size())) {
        const int idx = index_of(new_names, old_names[static_cast<size_t>(old_selected)]);
        out.selected = idx >= 0 ? idx : clamped(old_selected, new_names);
    } else {
        out.selected = clamped(old_selected, new_names);
    }

    for (const int i : old_multi) {
        if (i < 0 || i >= static_cast<int>(old_names.size())) continue;
        if (const int idx = index_of(new_names, old_names[static_cast<size_t>(i)]); idx >= 0)
            out.multi.push_back(idx);
    }
    std::ranges::sort(out.multi);
    return out;
}

} // namespace ui
