#pragma once

#include "vault/index.h"

#include <span>

namespace ui {

// Batch favorite rule (Phase 68 multiselect): if ANY node in the selection is
// not yet a favorite, the batch favorites everything (returns true); only when
// every node is already a favorite does it unfavorite them all (returns
// false). Mirrors the "select-all checkbox" convention. Empty selection: false
// (there is nothing to favorite).
[[nodiscard]] bool batch_favorite_target(
    std::span<const vault::IndexNode* const> nodes) noexcept;

} // namespace ui
