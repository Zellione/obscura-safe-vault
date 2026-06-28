#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "ui/favorites_screen.h"
#include "vault/vault.h"   // vault::SearchHit

namespace gfx { class Window; class FontAtlas; class Renderer; }
namespace vault { class Vault; }
namespace platform { class VaultRegistry; }

namespace ui {

// A flat grid of the galleries that *directly* carry one tag (Phase 22), reached
// by pressing Enter on a row of the tag-overview screen. Activating a gallery
// navigates to it in the normal grid; Esc/Backspace returns to the tag overview.
// Reuses the FavoritesScreen scaffolding (grid layout, selection, quick-switch).
class TagGalleries : public FavoritesScreen {
public:
    TagGalleries(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                 platform::VaultRegistry& registry, std::string active_path,
                 std::string tag);

protected:
    [[nodiscard]] std::vector<vault::SearchHit> fetch() const override;
    void draw_tile_content(gfx::Renderer& r, const vault::SearchHit& hit,
                           const SDL_FRect& box) override;
    void activate(const vault::SearchHit& hit, int index) override;
    void go_back() override;   // back to the tag overview, not the root grid
    [[nodiscard]] const char* title() const override { return title_.c_str(); }
    [[nodiscard]] const char* empty_hint() const override { return hint_.c_str(); }
    bool handle_extra_key(const SDL_KeyboardEvent& key) override;
    [[nodiscard]] const char* extra_hint() const override { return "[Tab] Images"; }
    [[nodiscard]] bool show_favorite_badge() const override { return false; }

private:
    std::string tag_;
    std::string title_;   // "Galleries tagged 'X'"
    std::string hint_;
};

} // namespace ui
