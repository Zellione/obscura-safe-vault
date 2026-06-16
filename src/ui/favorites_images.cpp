#include "ui/favorites_images.h"

#include "crypto/secure_mem.h"
#include "gfx/renderer.h"
#include "gfx/texture_cache.h"
#include "gfx/theme.h"
#include "ui/widgets.h"
#include "vault/index.h"

namespace ui {

std::vector<vault::SearchHit> FavoritesImages::fetch() const
{
    return vault_ref().list_favorite_images();
}

void FavoritesImages::update(double)
{
    if (pump_thumbs()) mark_dirty();   // off-thread thumbnail decode(s) landed
}

void FavoritesImages::activate(const vault::SearchHit&, int index)
{
    // Open the viewer over the whole favorites set, positioned on this image, so
    // prev/next iterate the favorites rather than one gallery. App rebuilds the
    // same list_favorite_images() ordering to construct the collection.
    request(NavKind::ToFavoriteViewer, "", index);
}

SDL_Texture* FavoritesImages::thumb_texture(const vault::IndexNode& node)
{
    if (node.meta.thumb_length == 0) return nullptr;
    const uint64_t key = node.meta.data_offset;
    if (SDL_Texture* t = cache_.get(key)) return t;

    if (failed_.contains(key) || worker_.pending(key)) return nullptr;
    crypto::SecureBytes sb;
    if (vault_ref().read_thumbnail(node, sb) != vault::VaultResult::Ok) return nullptr;
    worker_.submit(key, std::move(sb));
    return nullptr;
}

bool FavoritesImages::pump_thumbs()
{
    bool any = false;
    while (auto res = worker_.take_result()) {
        if (res->image) {
            cache_.get_or_upload(res->key, *res->image);
            any = true;
        } else {
            failed_.insert(res->key);
        }
    }
    return any;
}

void FavoritesImages::draw_tile_content(gfx::Renderer& r, const vault::SearchHit& hit,
                                        const SDL_FRect& box)
{
    r.draw_rect(box, gfx::Color{0, 0, 0, 255});   // black backing, never stretched
    if (SDL_Texture* tex = thumb_texture(*hit.node)) {
        float tw = 0;
        float th = 0;
        SDL_GetTextureSize(tex, &tw, &th);
        r.draw_image(tex, fit_rect(tw, th, box));
    } else {
        r.draw_text(font_ref(), box.x + 6, box.y + box.h * 0.5f - 14, "(no thumb)",
                    gfx::theme::TEXT_DIM);
    }
}

} // namespace ui
