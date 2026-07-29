#pragma once

#include <span>
#include <unordered_map>
#include <vector>

#include "ui/gallery_cover.h"  // CoverSpan

namespace vault { struct IndexNode; }

namespace ui {

// Memoised gallery cover resolution per listing.
// Covers for a gallery tile, resolved on first request and memoised until
// clear(). Keys are node POINTERS: the owner MUST clear() whenever its
// listing/tree may have changed (refresh / on_vault_changed / refetch) or
// lookups dereference stale keys.
class CoverCache {
public:
    [[nodiscard]] std::span<const CoverSpan> get(const vault::IndexNode& gallery);
    void clear() noexcept;

private:
    std::unordered_map<const vault::IndexNode*, std::vector<CoverSpan>> map_;
};

}  // namespace ui
