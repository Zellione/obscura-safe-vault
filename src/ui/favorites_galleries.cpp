#include "ui/favorites_galleries.h"

#include "gfx/renderer.h"
#include "gfx/theme.h"

namespace ui {

std::vector<vault::SearchHit> FavoritesGalleries::fetch() const
{
    return vault_ref().list_favorite_galleries();
}

void FavoritesGalleries::draw_tile_content(gfx::Renderer& r, const vault::SearchHit&,
                                           const SDL_FRect& box)
{
    // Folder glyph, matching the gallery grid's gallery tiles.
    const float ix = box.w * 0.18f;
    r.draw_round_rect({box.x + ix, box.y + box.h * 0.28f,
                       box.w - 2 * ix, box.h * 0.48f},
                      gfx::theme::RADIUS_SMALL, gfx::theme::FOLDER);
}

void FavoritesGalleries::activate(const vault::SearchHit& hit, int /*index*/)
{
    request(NavKind::ToGallery, hit.path, 0);
}

} // namespace ui
