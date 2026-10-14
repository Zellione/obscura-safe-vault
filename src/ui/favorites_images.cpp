#include "ui/favorites_images.h"

namespace ui {

std::vector<vault::SearchHit> FavoritesImages::fetch() const
{
    return vault::list_favorite_images(vault_ref());
}

void FavoritesImages::activate(const vault::SearchHit&, int index)
{
    // Open the viewer over the whole favorites set, positioned on this image, so
    // prev/next iterate the favorites rather than one gallery. App rebuilds the
    // same list_favorite_images() ordering to construct the collection.
    request(NavKind::ToFavoriteViewer, "", index);
}

} // namespace ui
