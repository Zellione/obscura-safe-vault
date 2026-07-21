#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "ui/favorites_images.h"
#include "vault/vault.h"   // vault::SearchHit

namespace gfx { class Window; class FontAtlas; class Renderer; class TextureCache; }
namespace vault { class Vault; }
namespace platform { class VaultRegistry; }

namespace ui {

// A flat grid of the images/videos that *directly* carry one tag (Phase 22
// follow-up), reached by pressing Tab on the tag-galleries view. Subclasses
// FavoritesImages to reuse its off-thread thumbnail decode + tile draw. Enter
// opens the viewer over the whole tagged set; Tab toggles back to the galleries
// view; Esc/Backspace returns to the tag overview.
class TagImages : public FavoritesImages {
public:
    TagImages(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
              gfx::TextureCache& cache, platform::VaultRegistry& registry,
              std::string active_path, std::string tag, bool initial_detail_open = false);

protected:
    [[nodiscard]] std::vector<vault::SearchHit> fetch() const override;
    void activate(const vault::SearchHit& hit, int index) override;
    void go_back() override;
    [[nodiscard]] const char* title() const override { return title_.c_str(); }
    [[nodiscard]] const char* empty_hint() const override { return hint_.c_str(); }
    bool handle_extra_key(const SDL_KeyboardEvent& key) override;
    [[nodiscard]] std::vector<ui::HelpEntry> extra_help_entries() const override
    {
        return {{"Tab", "Show galleries with this tag"}};
    }
    [[nodiscard]] bool show_favorite_badge() const override { return false; }

private:
    std::string tag_;
    std::string title_;   // "Images tagged 'X'"
    std::string hint_;
};

} // namespace ui
