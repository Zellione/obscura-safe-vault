#include "ui/favorites_galleries.h"

namespace ui {

std::vector<vault::SearchHit> FavoritesGalleries::fetch() const
{
    return vault::list_favorite_galleries(vault_ref());
}

void FavoritesGalleries::activate(const vault::SearchHit& hit, int /*index*/)
{
    request(NavKind::ToGallery, hit.path, 0);
}

} // namespace ui
