#pragma once

#include <span>
#include <string>
#include <vector>

namespace ui {

struct ListingRemap {
    int              selected  = 0;   // remapped nav selection
    std::vector<int> multi;           // remapped multi-selection (ascending)
    bool             unchanged = false;
};

// Remap a listing's selection and multi-selection from old names to new names.
// Semantics: `unchanged` = element-wise equality. `selected`: the old selected
// NAME's index in `new_names` when found; otherwise clamp(old_selected, 0,
// max(0, new_size-1)). `multi`: each valid old index → its name's new index,
// dropped when the name vanished. Sibling names are unique, so first-match is exact.
[[nodiscard]] ListingRemap remap_listing(std::span<const std::string> old_names,
                                         std::span<const std::string> new_names,
                                         int                          old_selected,
                                         std::span<const int>         old_multi);

} // namespace ui
