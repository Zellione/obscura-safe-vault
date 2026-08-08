#pragma once

// Phase 56: where the viewer's current item lands after a background import
// reallocated the index tree. Pure — a vector of paths in, a decision out — so
// the "keep the user's view" rule is testable without a window or a vault.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "vault/index.h"

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

// Remove index-parallel (image, path) pairs whose node is null — used after a
// vault change re-resolves a collection album's paths and some vanished.
// Returns the number of pairs removed.
size_t compact_album(std::vector<const vault::IndexNode*>& images,
                     std::vector<std::string>&             paths);

// Phase 46 made galleries mixed: sub-galleries and media are siblings, and the
// sorted listing partitions galleries first. The grid and the search screens
// index that FULL listing; the viewer's album is media-only. These two convert
// between the spaces at the boundary — without the conversion, every media item
// after the sub-gallery block opens shifted by the number of sub-galleries.
//
// Full-listing index -> album (media-only) index. Counts the media entries
// before `listing_index`; out-of-range input clamps into the listing.
[[nodiscard]] int media_index_in_listing(std::span<const vault::IndexNode* const> listing,
                                         int listing_index);

// Album (media-only) index -> full-listing index of that media entry.
// Clamped into the listing's media range; 0 when the listing has no media.
[[nodiscard]] int listing_index_of_media(std::span<const vault::IndexNode* const> listing,
                                         int media_index);

} // namespace ui
