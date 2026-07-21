#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "image/decode_worker.h"
#include "ui/favorites_screen.h"
#include "vault/vault.h"   // vault::SearchHit

namespace gfx { class Window; class FontAtlas; class Renderer; class TextureCache; }
namespace vault { class Vault; struct IndexNode; }
namespace platform { class VaultRegistry; }

namespace ui {

// A flat grid of every favorited *image* across the whole vault (Phase 13).
// Activating one opens the viewer in that image's home gallery. Reached from the
// gallery grid with F; Esc/Backspace returns to the root gallery. Thumbnails are
// decoded off-thread (decrypt into mlock'd memory, upload to the GPU, never to
// disk), exactly like the gallery grid.
class FavoritesImages : public FavoritesScreen {
public:
    FavoritesImages(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                    gfx::TextureCache& cache, platform::VaultRegistry& registry,
                    std::string active_path, bool initial_detail_open = false)
        : FavoritesScreen(win, font, vault, registry, std::move(active_path),
                          initial_detail_open),
          cache_(cache) {}

    void update(double dt) override;

protected:
    [[nodiscard]] std::vector<vault::SearchHit> fetch() const override;
    void draw_tile_content(gfx::Renderer& r, const vault::SearchHit& hit,
                           const SDL_FRect& box) override;
    void activate(const vault::SearchHit& hit, int index) override;
    [[nodiscard]] const char* title() const override { return "Favorite Images"; }
    [[nodiscard]] const char* empty_hint() const override
    {
        return "No favorite images yet. Press [B] on an image (grid or viewer) to bookmark it.";
    }

private:
    SDL_Texture* thumb_texture(const vault::IndexNode& node);
    bool         pump_thumbs();   // upload finished off-thread decodes

    gfx::TextureCache&           cache_;
    image::DecodeWorker          worker_{image::decode_wake_event()};
    std::unordered_set<uint64_t> failed_;   // thumbs that gave up decoding
};

} // namespace ui
