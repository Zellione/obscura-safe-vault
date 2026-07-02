#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>
#include <vector>

#include "ui/keybindings.h"   // is_quick_switch_key (centralised, Phase 25)

namespace gfx { class Renderer; class FontAtlas; }
namespace platform { class VaultRegistry; }

namespace ui {

// Global `` ` `` (grave) overlay to jump to another vault from any browsing/viewing
// screen (Phase 14 PR5). Lists every known vault (registry, paths only); choosing
// one hands the host screen a path to emit as NavKind::ToUnlock (which locks the
// current active vault and unlocks the chosen one). Choosing the already-active
// vault, or Esc, just closes. The overlay holds no key — it only names a path; all
// wiping happens in App's lock-on-switch.
class QuickSwitch {
public:
    QuickSwitch(platform::VaultRegistry& registry, std::string active_path);

    void open();                                       // rebuild list + activate
    void close() noexcept { active_ = false; }
    [[nodiscard]] bool active() const noexcept { return active_; }

    [[nodiscard]] bool handle_event(const SDL_Event& e);        // true if consumed
    [[nodiscard]] bool consume_choice(std::string& path_out);   // a vault was chosen
    void render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const;

private:
    void choose();

    platform::VaultRegistry& registry_;
    std::string              active_path_;

    bool                               active_ = false;
    std::vector<std::filesystem::path> vaults_;
    int                                sel_ = 0;
    std::string                        chosen_;       // drained by consume_choice
    bool                               has_choice_ = false;
};

// is_quick_switch_key() now lives in ui/keybindings.h (Phase 25) alongside the
// other layout-robust shortcut helpers; included above so existing users of this
// header keep resolving it unchanged.

} // namespace ui
