#pragma once

#include <SDL3/SDL.h>

#include <vector>

#include "ui/favorites_screen.h"
#include "vault/vault.h"   // vault::SearchHit

namespace gfx { class Window; class FontAtlas; class Renderer; class TextureCache; }
namespace vault { class Vault; struct IndexNode; }
namespace platform { class VaultRegistry; }

namespace ui {

// A flat grid of every favorited image/video across the whole vault (Phase 13).
// Activating one opens the favorites-scoped viewer. Reached from the
// gallery grid with F; Esc/Backspace returns to the root gallery. Thumbnails are
// decoded off-thread (decrypt into mlock'd memory, upload to the GPU, never to
// disk), exactly like the gallery grid.
class FavoritesImages : public FavoritesScreen {
public:
    using FavoritesScreen::FavoritesScreen;

protected:
    [[nodiscard]] std::vector<vault::SearchHit> fetch() const override;
    void activate(const vault::SearchHit& hit, int index) override;
    [[nodiscard]] const char* title() const override { return "Favorite Images"; }
    [[nodiscard]] const char* empty_hint() const override
    {
        return "No favorite images yet. Press [B] on an image (grid or viewer) to bookmark it.";
    }
};

} // namespace ui
