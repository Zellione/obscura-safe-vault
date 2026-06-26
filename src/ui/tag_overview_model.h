#pragma once

// Pure, SDL/vault-free presentation logic for the Phase 22 tag-overview screen.
//
// The overview is a flat list of distinct tags, each with how many galleries and
// images *directly* carry it (the counts are produced by the vault layer). This
// header owns only the sort + typed-prefix filter the screen applies on top.

#include <string>
#include <string_view>
#include <vector>

namespace ui {

// One row of the tag overview: a distinct tag and how many galleries / leaf media
// directly carry it. Direct-tag counts only (no Phase 12 cascade), so a gallery
// tag never inflates its descendants. `image_count` covers non-gallery media
// (images and videos); the field name follows the Phase 22 spec.
struct TagTally {
    std::string tag;
    int         gallery_count = 0;
    int         image_count   = 0;

    [[nodiscard]] int total() const noexcept { return gallery_count + image_count; }
};

// Sort key for the overview list.
enum class TagSort { Name, Count };

// Sort `tags` in place.
//   Name  — case-insensitive ascending by tag.
//   Count — descending by total() (gallery + image), ties broken by
//           case-insensitive ascending name.
void sort_tags(std::vector<TagTally>& tags, TagSort sort);

// Return the subset of `tags` whose tag begins with `prefix` (case-insensitive).
// An empty prefix returns a copy of all of `tags`. Input order is preserved.
[[nodiscard]] std::vector<TagTally> filter_tags(const std::vector<TagTally>& tags,
                                                std::string_view prefix);

} // namespace ui
