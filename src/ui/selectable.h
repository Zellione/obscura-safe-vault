#pragma once

// Phase 53: which children of a gallery may be put in the export/move
// selection.
//
// This is the single source of truth for that rule. It was previously spelled
// out inline in three places — the Space guard, the Space/open branch, and
// Ctrl+A — which is exactly how they drift apart. A type Space refuses must
// never reach a mass operation, because export/move would then receive a node
// they cannot process.
//
// Images and videos qualify (Phase 53 taught export_one_media to write video
// originals); a gallery qualifies because mass-move and mass-tag accept one.
// Nothing else does.
//
// Pure: no SDL, no vault I/O — just the node types.

#include <span>
#include <vector>

#include "vault/index.h"

namespace ui {

[[nodiscard]] bool is_selectable(const vault::IndexNode& node) noexcept;

// Indices into `children` that `is_selectable` accepts, ascending.
[[nodiscard]] std::vector<int> selectable_indices(
    std::span<const vault::IndexNode* const> children);

} // namespace ui
