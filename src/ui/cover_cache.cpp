#include "ui/cover_cache.h"

#include "ui/gallery_cover.h"

namespace ui {

std::span<const CoverSpan> CoverCache::get(const vault::IndexNode& gallery)
{
    const auto it = map_.find(&gallery);
    if (it != map_.end()) return it->second;
    return map_.emplace(&gallery, resolve_covers(gallery)).first->second;
}

void CoverCache::clear() noexcept { map_.clear(); }

}  // namespace ui
