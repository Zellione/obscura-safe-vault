#include "ui/tag_images.h"

#include <utility>

#include "vault/vault_search.h"

namespace ui {

TagImages::TagImages(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                     gfx::TextureCache& cache, platform::VaultRegistry& registry,
                     std::string active_path, std::string tag)
    : FavoritesImages(win, font, vault, cache, registry, std::move(active_path)),
      tag_(std::move(tag))
{
    title_ = "Images tagged '" + tag_ + "'";
    hint_  = "No images carry the tag '" + tag_ + "'.";
}

std::vector<vault::SearchHit> TagImages::fetch() const
{
    return vault::VaultSearch(vault_ref()).images_with_tag(tag_);
}

void TagImages::activate(const vault::SearchHit& /*hit*/, int index)
{
    // Open the viewer over the whole tagged set so prev/next iterate it; App
    // rebuilds the same images_with_tag() ordering to construct the collection.
    request(NavKind::ToTagViewer, tag_, index);
}

void TagImages::go_back()
{
    request(NavKind::ToTagOverview);
}

bool TagImages::handle_extra_key(const SDL_KeyboardEvent& key)
{
    if (key.key == SDLK_TAB) { request(NavKind::ToTagGalleries, tag_, 0); return true; }
    return false;
}

} // namespace ui
