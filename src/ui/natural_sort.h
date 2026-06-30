#pragma once

#include <string_view>

namespace ui {

// Three-way "human" comparison of two names: each maximal run of ASCII digits is
// compared by numeric value (so "img2" < "img10"), and every other character is
// compared case-insensitively. Returns < 0, 0, or > 0. Equal numeric values are
// ordered with fewer leading zeros first ("1" < "01"); strings that differ only
// in letter case compare equal (0). Pure: no allocation, no SDL, no vault.
[[nodiscard]] int natural_compare(std::string_view a, std::string_view b);

// Strict-weak-ordering predicate for std::sort / std::stable_sort.
[[nodiscard]] inline bool natural_less(std::string_view a, std::string_view b)
{
    return natural_compare(a, b) < 0;
}

} // namespace ui
