#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <string_view>
#include <vector>

namespace gfx { class Renderer; class FontAtlas; class Window; }
namespace vault { class Vault; }

namespace ui {

// A modal tag editor panel for editing tags on a gallery or image (Phase 12).
// Shows the node's current tags, lets the user add a new tag (type + Enter),
// and remove existing tags (select + Delete). Applies changes immediately via
// vault.add_tag / vault.remove_tag.
//
// Usage:
//   1. open(node_path) to activate with a specific node
//   2. handle_event() for text input and key handling
//   3. render() to draw the modal
//   4. close() or let it auto-close on Esc
class TagEditor {
public:
    TagEditor(vault::Vault& vault, gfx::Window& win) : vault_(vault), win_(win) {}

    void open(std::string node_path);  // activate for a specific node
    void close();                       // deactivate and clear
    [[nodiscard]] bool active() const noexcept { return active_; }

    // Handle text input and keyboard events. Returns true if consumed.
    [[nodiscard]] bool handle_event(const SDL_Event& e);

    void render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H);

private:
    void refresh_tags();              // reload the node's current tags from the vault
    void select_tag(std::string_view tag);  // select `tag` (ci) so the list scrolls to it

    vault::Vault&        vault_;
    gfx::Window&         win_;
    bool                 active_ = false;
    std::string          node_path_;
    std::vector<std::string> tags_;          // current node tags
    std::vector<std::string> inherited_;     // read-only ancestor-gallery tags
    int                  selected_ = 0;      // index of the selected tag to delete
    std::string          new_tag_buf_;       // input buffer for adding a new tag
    std::string          error_;             // transient error message
};

} // namespace ui
