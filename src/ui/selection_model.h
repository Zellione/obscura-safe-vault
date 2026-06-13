#pragma once

// Pure, SDL-free multi-selection state for the gallery grid (Phase 10 export).
//
// Holds a set of selected item indices (positions within the current leaf
// gallery's child list). The grid clears it whenever the navigated path
// changes, so indices are only ever interpreted against the current listing.

#include <set>
#include <vector>

namespace ui {

class SelectionModel {
public:
    // Add `i` if absent, remove it if present.
    void toggle(int i);

    // Drop all selected indices.
    void clear();

    [[nodiscard]] bool contains(int i) const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::size_t count() const;

    // Selected indices in ascending order.
    [[nodiscard]] std::vector<int> indices() const;

private:
    std::set<int> items_;
};

} // namespace ui
