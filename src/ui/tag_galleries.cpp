#include "ui/tag_galleries.h"

#include <utility>

#include "vault/vault_search.h"

namespace ui {

TagGalleries::TagGalleries(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                           gfx::TextureCache& cache, platform::VaultRegistry& registry,
                           std::string tag, const CollectionOps& ops)
    : FavoritesScreen(win, font, vault, cache, registry, ops), tag_(std::move(tag))
{
    title_ = "Galleries tagged '" + tag_ + "'";
    hint_  = "No galleries carry the tag '" + tag_ + "'.";
}

std::vector<vault::SearchHit> TagGalleries::fetch() const
{
    return vault::VaultSearch(vault_ref()).galleries_with_tag(tag_);
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
