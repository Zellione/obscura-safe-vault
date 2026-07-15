#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "ui/nav_model.h"
#include "ui/quick_switch.h"
#include "ui/screen.h"
#include "ui/tag_overview_model.h"   // ui::TagTally, TagSort

namespace gfx { class Window; class FontAtlas; class Renderer; }
namespace vault { class Vault; }
namespace platform { class VaultRegistry; }

namespace ui {

// The Phase 22 tag-overview screen: a first-class Screen (opened with Shift+T
// from the gallery grid) listing every distinct tag in the vault with how many
// galleries and images *directly* carry it. Up/Down navigate; Enter opens a
// galleries-only view (TagGalleries) of the galleries carrying the focused tag;
// Tab toggles the sort (by name / by count); typing filters by tag prefix;
// Esc/Backspace clears the filter or returns to the gallery grid.
//
// All counting lives in the VaultSearch facade; all sort/filter lives in the
// pure tag_overview_model — this screen is only SDL plumbing.
class TagOverviewScreen : public Screen {
public:
    TagOverviewScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                      platform::VaultRegistry& registry, std::string active_path);

    void on_enter() override;
    void on_exit() override;
    void handle_event(const SDL_Event& e) override;
    void render(gfx::Renderer& r) override;

    [[nodiscard]] std::vector<ui::HelpGroup> help_groups() const override;

private:
    void reload();          // fetch the overview from the vault, then rebuild()
    void rebuild();         // shown_ = sort(filter(all_)); re-clamp selection
    void open_selected();   // Enter → TagGalleries for the focused tag
    [[nodiscard]] int row_at(float my) const;   // mouse y → row index (-1 = none)

    gfx::Window&    win_;
    gfx::FontAtlas& font_;
    vault::Vault&   vault_;
    QuickSwitch     quick_switch_;   // ` overlay: jump to another vault
    NavModel        nav_;            // selection over shown_ (one row each)

    std::vector<TagTally> all_;      // full overview, as returned by the vault
    std::vector<TagTally> shown_;    // filtered + sorted view that is navigated
    TagSort               sort_ = TagSort::Name;
    std::string           filter_;   // typed case-insensitive tag prefix
};

} // namespace ui
