#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "image/decode_worker.h"
#include "ui/nav_model.h"
#include "ui/screen.h"
#include "vault/vault.h"   // vault::SearchHit

namespace gfx { class Window; class FontAtlas; class Renderer; class TextureCache; }
namespace vault { class Vault; struct IndexNode; }

namespace ui {

// A flat grid of every favorited *image* across the whole vault (Phase 13).
// Activating one opens the viewer in that image's home gallery. Reached from the
// gallery grid with F; Esc/Backspace returns to the root gallery. Thumbnails are
// decoded off-thread, exactly like the gallery grid (decrypt into mlock'd memory,
// upload to the GPU, never to disk).
class FavoritesImages : public Screen {
public:
    FavoritesImages(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                    gfx::TextureCache& cache);

    void on_enter() override;
    void handle_event(const SDL_Event& e) override;
    void update(double dt) override;
    void render(gfx::Renderer& r) override;

private:
    void open_selected();
    [[nodiscard]] int  hit_test(float mx, float my) const;
    SDL_Texture*       thumb_texture(const vault::IndexNode& node);
    bool               pump_thumbs();   // upload finished off-thread decodes

    gfx::Window&       win_;
    gfx::FontAtlas&    font_;
    vault::Vault&      vault_;
    gfx::TextureCache& cache_;
    NavModel           nav_;   // selection only
    std::vector<vault::SearchHit> favs_;
    int                cols_ = 1;

    image::DecodeWorker          worker_{image::decode_wake_event()};
    std::unordered_set<uint64_t> failed_;   // thumbs that gave up decoding
};

} // namespace ui
