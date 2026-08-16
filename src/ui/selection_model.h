#pragma once

// Pure, SDL-free multi-selection state for the gallery grid (Phase 10 export).
//
// Holds a set of selected item indices (positions within the current leaf
// gallery's child list). The grid clears it whenever the navigated path
// changes, so indices are only ever interpreted against the current listing.

#include <cstdint>
#include <optional>
#include <set>
#include <utility>
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

    // Phase 86: select `i` without ever deselecting and WITHOUT establishing
    // a range anchor — the grid's filtered range fill adds items one by one
    // and must not move the anchor the way a hand toggle does.
    void select(int i);

    // Phase 86: select every index in [min(a,b), max(a,b)] inclusive. One
    // revision bump for the whole span. Does not move the range anchor.
    void select_range(int a, int b);

    // Phase 86: the span a range-fill key (Shift+Space) pressed at `focus`
    // should select, or nullopt when there is no anchor to span from. The
    // anchor is the most recent index toggled ON; when the focus sits on the
    // anchor itself, the span falls back to the previously toggled-on index —
    // covering both "anchor then extend" and "select two, then fill between".
    // Ctrl+A / select_all never establishes an anchor.
    [[nodiscard]] std::optional<std::pair<int, int>> range_for(int focus) const;

    // Monotonic change counter, bumped by every mutation. Lets a consumer cache
    // work derived from the selection without comparing the set itself — count()
    // is not sufficient, since {1} and {2} are both size 1.
    [[nodiscard]] uint64_t revision() const noexcept { return revision_; }

private:
    std::set<int> items_;
    uint64_t      revision_ = 0;
    int           anchor_      = -1;  // most recent index toggled ON
    int           prev_anchor_ = -1;  // the toggled-ON index before that
};

} // namespace ui
