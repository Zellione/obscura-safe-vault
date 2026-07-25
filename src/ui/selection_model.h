#pragma once

// Pure, SDL-free multi-selection state for the gallery grid (Phase 10 export).
//
// Holds a set of selected item indices (positions within the current leaf
// gallery's child list). The grid clears it whenever the navigated path
// changes, so indices are only ever interpreted against the current listing.

#include <cstdint>
#include <set>
#include <vector>

namespace ui {

class SelectionModel {
public:
    // Add `i` if absent, remove it if present.
    void toggle(int i);

    // Drop all selected indices.
    void clear();

    // Select every index in [0, count). Phase 53's Ctrl+A. A count <= 0 leaves
    // the selection untouched rather than clearing it — "select all of nothing"
    // is not a request to deselect.
    void select_all(int count);

    // True when [0, count) are all selected. Drives Ctrl+A's toggle: a second
    // press on a fully-selected listing clears instead of re-selecting. False
    // for count <= 0, so an empty gallery never reports itself as fully
    // selected.
    [[nodiscard]] bool all_selected(int count) const;

    [[nodiscard]] bool contains(int i) const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::size_t count() const;

    // Selected indices in ascending order.
    [[nodiscard]] std::vector<int> indices() const;

    // Monotonic change counter, bumped by every mutation. Lets a consumer cache
    // work derived from the selection without comparing the set itself — count()
    // is not sufficient, since {1} and {2} are both size 1.
    [[nodiscard]] uint64_t revision() const noexcept { return revision_; }

private:
    std::set<int> items_;
    uint64_t      revision_ = 0;
};

} // namespace ui
