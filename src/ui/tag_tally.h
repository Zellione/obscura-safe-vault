#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "ui/tag_inherit.h"   // ui::tag_ci_equal

namespace ui {

// One tag from the union of a multi-selection's own tags, with how many of
// the selected nodes carry it.
struct TagTallyEntry {
    std::string tag;
    int         count = 0;
};

// Union of tags across `per_node_tags` (one inner vector per selected node's
// own tags), each entry annotated with how many nodes carry it. Tag identity
// is case-insensitive (matching Vault::add_tag's own dedup); the first-seen
// casing is kept; entries appear in first-seen order. A node with no tags
// contributes nothing. Pure — no vault, no SDL (Phase 45 Part 2).
[[nodiscard]] inline std::vector<TagTallyEntry> compute_tag_tally(
    const std::vector<std::vector<std::string>>& per_node_tags)
{
    std::vector<TagTallyEntry> out;
    for (const auto& node_tags : per_node_tags) {
        for (const std::string& tag : node_tags) {
            auto it = std::ranges::find_if(
                out, [&](const TagTallyEntry& e) { return tag_ci_equal(e.tag, tag); });
            if (it != out.end()) {
                ++it->count;
            } else {
                out.push_back({.tag = tag, .count = 1});
            }
        }
    }
    return out;
}

} // namespace ui
