#pragma once

// Phase 56: where the viewer's current item lands after a background import
// reallocated the index tree. Pure — a vector of paths in, a decision out — so
// the "keep the user's view" rule is testable without a window or a vault.

#include <string>
#include <string_view>
#include <vector>

namespace ui {

struct AlbumRebind {
    int  index    = 0;      // the index to display
    bool preserve = false;  // true: same item, keep zoom/pan/scroll/playback as-is
};

// Locate `current_path` in the freshly listed `new_paths`. Found → that index,
// preserve. Gone (deleted, moved out, or the album emptied) → `current_index`
// clamped into range, and the caller refits as it did before this phase.
[[nodiscard]] AlbumRebind rebind_album_index(const std::vector<std::string>& new_paths,
                                             std::string_view current_path,
                                             int current_index);

} // namespace ui
