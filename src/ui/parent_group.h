#pragma once

#include <span>
#include <string>
#include <vector>

namespace ui {

// One source gallery and the media names selected inside it.
struct ParentGroup {
    std::string              parent;   // slash-path; "" = vault root
    std::vector<std::string> names;
};

// Group full slash-paths ("gal/sub/name.jpg") by their parent gallery
// (Phase 68). Collection screens (favorites, tags, search results) list items
// from different parents, but vault::transfer_images and the export flow work
// per source gallery — this is the bridge. Group order follows first
// appearance; name order within a group is preserved.
[[nodiscard]] std::vector<ParentGroup> group_by_parent(
    std::span<const std::string> full_paths);

} // namespace ui
