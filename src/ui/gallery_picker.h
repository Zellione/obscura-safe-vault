#pragma once

// Pure, SDL-free model for a filterable, scrollable list of gallery paths —
// shared by TransferDialog's PickGallery stage and CombineDialog's PickTarget
// stage (Phase 44). Filtering reuses ui::tokenize/ui::matches (search_model.h),
// treating each path as a bare name with no tags. Scroll windowing mirrors
// tag_overview.cpp's compute_geom (keep the selection roughly centred once the
// list is taller than the viewport).

#include <string>
#include <string_view>
#include <vector>

#include "ui/text_input_model.h"

namespace ui {

class GalleryPickerModel {
public:
    // Replaces the full (unfiltered) item list; resets filter text, filter-open
    // state, and selection back to 0. Clears multi-select mode and checked items.
    void set_items(std::vector<std::string> items);

    // An item that is always appended to filtered()'s end, exempt from the
    // filter query — e.g. a pinned "create new…" affordance that must stay
    // reachable no matter what the user has typed. Cleared by set_items().
    // If the pinned item also matches the current filter naturally, it is
    // not duplicated.
    void set_pinned_suffix(std::string item);

    // Multi-select mode (opt-in; default is single-select). Cleared by set_items().
    void set_multi(bool on) noexcept { multi_ = on; }
    [[nodiscard]] bool multi() const noexcept { return multi_; }

    // Toggle the checked state of the current row (filtered()[selected()]).
    // No-op unless multi() is true. The pinned-suffix row (if current) is ignored.
    void toggle_checked();

    // Check if an item is checked (by exact path match).
    [[nodiscard]] bool is_checked(std::string_view item) const;

    // All checked items in original items_ order (not check order).
    [[nodiscard]] std::vector<std::string> checked() const;

    // '/' toggles typing a filter query; closing does NOT clear the filter text
    // or its effect on filtered() — only set_items()/filter_clear() do that.
    void open_filter() noexcept { filter_open_ = true; }
    void close_filter() noexcept { filter_open_ = false; }
    [[nodiscard]] bool filter_open() const noexcept { return filter_open_; }

    // The filter query as a full editable field (Phase 54). Hosts route SDL
    // events into it through handle_text_input_event(), then call refilter()
    // when its revision() moved. The three convenience mutators below stay for
    // non-SDL callers and remain the thing the unit tests drive.
    [[nodiscard]] ITextInput& filter_input() noexcept { return filter_; }
    void refilter() { rebuild_filtered(); }

    void filter_append(std::string_view utf8);
    void filter_backspace();
    void filter_clear();
    [[nodiscard]] std::string_view filter() const noexcept { return filter_.view(); }

    // Move the selection by `delta` within the filtered list, clamped to
    // [0, filtered().size()-1] (0 when the filtered list is empty).
    void move(int delta) noexcept;

    [[nodiscard]] const std::vector<std::string>& filtered() const noexcept { return filtered_; }
    [[nodiscard]] int selected() const noexcept { return selected_; }

    // Scroll window: `first` is the index of the first visible row, `visible`
    // the number of rows drawn, given `visible_rows` rows fit on screen.
    struct Geom {
        int first;
        int visible;
    };
    [[nodiscard]] Geom geom(int visible_rows) const noexcept;

private:
    void rebuild_filtered();

    std::vector<std::string> items_;
    TextInputModel            filter_;
    bool                       filter_open_ = false;
    std::vector<std::string>  filtered_;
    int                        selected_ = 0;
    std::string               pinned_suffix_;   // empty = none
    bool                       multi_ = false;
    std::vector<std::string>  checked_;   // items_ indices of checked items
};

// Remove every path that is a strict descendant of another path in the list.
// "a" drops "a/b" and "a/b/c"; "ab" is NOT a descendant of "a" (separator matters).
// Returns kept paths in sorted order.
[[nodiscard]] std::vector<std::string> drop_descendant_paths(std::vector<std::string> paths);

} // namespace ui
