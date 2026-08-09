#include "ui/favorite_batch.h"

#include <algorithm>

namespace ui {

bool batch_favorite_target(std::span<const vault::IndexNode* const> nodes) noexcept
{
    return std::ranges::any_of(nodes, [](const vault::IndexNode* n) {
        return n != nullptr && !n->favorite;
    });
}

} // namespace ui
