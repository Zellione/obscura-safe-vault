#pragma once

// Phase 51: what is DIRECTLY inside a sub-gallery, for its grid tile.
//
// Deliberately NOT the recursive ui::count_subtree the [D] detail panel uses:
// the tile answers "what is in this folder", the panel answers "what is under
// this folder". They disagree on deep trees by design, so format_tile_counts'
// wording must stay unambiguous about which question it answers.

#include <span>
#include <string>

#include "ui/delete_summary.h"   // SubtreeCounts

namespace vault { struct IndexNode; }

namespace ui {

// Tally `node`'s IMMEDIATE children by kind. Nested content is not counted and
// `bytes` is left at 0 (a tile has no room for a size).
[[nodiscard]] SubtreeCounts direct_child_counts(const vault::IndexNode& node);

// Tile label: "3 galleries · 12 items" / "1 gallery · 1 item" / "12 items" /
// "4 galleries" / "empty". "items" is images + videos COMBINED. Zero sides are
// dropped; plural follows each count.
[[nodiscard]] std::string format_tile_counts(const SubtreeCounts& c);

// Per-LISTING reservation predicate: true when any child is a gallery, i.e.
// when the grid must reserve a count row at all. Mirrors the Phase 49
// any_chips_to_show contract — reserve per gallery, never per tile, so a
// listing with no sub-galleries looks exactly as it did before.
[[nodiscard]] bool any_tile_counts_to_show(std::span<const vault::IndexNode* const> children);

} // namespace ui
