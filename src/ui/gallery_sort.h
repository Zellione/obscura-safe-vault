#pragma once

// Pure, SDL-free presentation logic for the Phase 37 persisted per-gallery
// sort order. Operates on vault::IndexNode/vault::SortKey directly (no
// Vault& dependency) — same "pure" contract as natural_sort.h and
// tag_overview_model.h.

#include <span>
#include <string>
#include <vector>

#include "vault/index.h"

namespace ui {

// Sort a gallery's children for display. Sub-galleries always precede
// non-gallery children ("folders first"), regardless of `key` — in practice
// this is a no-op safety net, since one real gallery's children are never
// actually mixed (a gallery holds either sub-galleries or images/videos,
// never both). Within each group: Manual preserves `nodes`' input order;
// NameAsc/NameDesc delegates to ui::natural_less (Phase 24); DateAsc/DateDesc
// compares created_ts (0 for Gallery children, so a sub-gallery listing sorts
// stably under these keys — they only do meaningful work in a leaf gallery);
// SizeAsc/SizeDesc compares orig_size likewise. Stable throughout, so ties
// preserve relative insertion order.
[[nodiscard]] std::vector<const vault::IndexNode*>
sort_children(std::span<const vault::IndexNode*> nodes, vault::SortKey key);

// Cycle to the next sort key in the fixed UI order:
// Manual -> NameAsc -> NameDesc -> DateAsc -> DateDesc -> SizeAsc -> SizeDesc -> Manual.
[[nodiscard]] vault::SortKey next_sort_key(vault::SortKey current) noexcept;

// Short footer label for a sort key ("Name ↑", "Date ↓", ...). Empty for
// Manual — callers use this to hide the footer sort indicator in that case.
[[nodiscard]] std::string sort_key_label(vault::SortKey key);

} // namespace ui
