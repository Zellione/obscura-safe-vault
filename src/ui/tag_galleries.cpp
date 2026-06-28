#include "ui/tag_galleries.h"

#include <utility>

#include "gfx/renderer.h"
#include "gfx/theme.h"
#include "vault/vault_search.h"

namespace ui {

TagGalleries::TagGalleries(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                           platform::VaultRegistry& registry, std::string active_path,
                           std::string tag)
    : FavoritesScreen(win, font, vault, registry, std::move(active_path)),
      tag_(std::move(tag))
{
    title_ = "Galleries tagged '" + tag_ + "'";
    hint_  = "No galleries carry the tag '" + tag_ + "'.";
}

std::vector<vault::SearchHit> TagGalleries::fetch() const
{
    return vault::VaultSearch(vault_ref()).galleries_with_tag(tag_);
}

void TagGalleries::draw_tile_content(gfx::Renderer& r, const vault::SearchHit&,
                                     const SDL_FRect& box)
{
    // Folder glyph, matching the gallery grid + favorites-galleries tiles.
    const float ix = box.w * 0.18f;
    r.draw_round_rect({box.x + ix, box.y + box.h * 0.28f,
                       box.w - 2 * ix, box.h * 0.48f},
                      gfx::theme::RADIUS_SMALL, gfx::theme::FOLDER);
}

void TagGalleries::activate(const vault::SearchHit& hit, int /*index*/)
{
    request(NavKind::ToGallery, hit.path, 0);
}

void TagGalleries::go_back()
{
    request(NavKind::ToTagOverview);
}

bool TagGalleries::handle_extra_key(const SDL_KeyboardEvent& key)
{
    if (key.key == SDLK_TAB) { request(NavKind::ToTagImages, tag_, 0); return true; }
    return false;
}

} // namespace ui
