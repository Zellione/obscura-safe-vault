#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "ui/nav_model.h"
#include "ui/quick_switch.h"
#include "ui/screen.h"
#include "ui/tag_overview_model.h"   // ui::TagTally, TagSort
#include "ui/text_input_model.h"
#include "ui/widgets.h"
#include "vault/index.h"

namespace gfx { class Window; class FontAtlas; class Renderer; }
namespace vault { class Vault; }
namespace platform { class VaultRegistry; class FileDialog; }

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
                      platform::VaultRegistry& registry, std::string active_path,
                      platform::FileDialog& file_dialog);

    void on_enter() override;
    void on_exit() override;
    void handle_event(const SDL_Event& e) override;
    void update(double dt) override;
    void render(gfx::Renderer& r) override;

    [[nodiscard]] std::vector<ui::HelpGroup> help_groups() const override;

private:
    void reload();          // fetch the overview from the vault, then rebuild()
    void rebuild();         // shown_ = sort(filter(all_)); re-clamp selection
    void handle_filter_event(const SDL_Event& e);   // while filtering_
    void close_filter();
    void open_selected();   // Enter → TagGalleries for the focused tag
    [[nodiscard]] int row_at(float my) const;   // mouse y → row index (-1 = none)
    void handle_prompt_key_event(const SDL_Event& e);  // handle SDL_EVENT_KEY_DOWN while prompting
    void handle_key_down_in_browse_mode(const SDL_KeyboardEvent& key);

    // Phase 55: Ctrl+I → pick a .json tag dictionary; the result is drained by
    // update() and applied in one settings commit.
    void open_tag_dict_picker();
    void import_tag_dict(const std::string& path);
    void render_import_summary(gfx::Renderer& r, float win_w, float win_h);

    gfx::Window&          win_;
    gfx::FontAtlas&       font_;
    vault::Vault&         vault_;
    platform::FileDialog& file_dialog_;
    QuickSwitch     quick_switch_;   // ` overlay: jump to another vault
    NavModel        nav_;            // selection over shown_ (one row each)

    std::vector<TagTally> all_;      // full overview, as returned by the vault
    std::vector<TagTally> shown_;    // filtered + sorted view that is navigated
    TagSort               sort_ = TagSort::Name;
    TextInputModel        filter_;   // typed case-insensitive tag prefix
    TextFieldChrome       filter_chrome_;
    // Phase 54: '/' opens filter mode. Before it, nothing ever routed text into
    // filter_ — the browse-mode gate could not become true — so the documented
    // type-to-filter never worked. An explicit mode is what makes it reachable
    // without stealing the bare letter keys (E opens the description prompt).
    bool                  filtering_ = false;

    // Phase 51: tag description editing
    bool            prompting_ = false;
    bool            prompt_skip_text_input_ = false;  // Suppress the opening keypress's text event
    TextInputModel  prompt_buf_{vault::INDEX_MAX_TAG_DESC_BYTES};
    TextFieldChrome prompt_chrome_;
    std::string     error_;

    // Phase 55: the tag-dictionary import summary. Non-empty exactly while the
    // modal is up; any key dismisses it.
    std::vector<std::string> import_summary_;
};

} // namespace ui
