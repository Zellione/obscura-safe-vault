#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <string_view>
#include <vector>

#include "ui/tag_tally.h"   // ui::TagTallyEntry, compute_tag_tally

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

    void open(std::string node_path);  // activate for a single node
    // Activate for 2+ nodes at once (Phase 45 Part 2): shows the union of
    // their tags, each annotated with how many of `node_paths` carry it.
    // Adding a tag applies it to every node lacking it; removing a tag
    // applies to every node carrying it. `node_paths` must be non-empty.
    void open_multi(std::vector<std::string> node_paths);
    void close();                       // deactivate and clear
    [[nodiscard]] bool active() const noexcept { return active_; }

    // Handle text input and keyboard events. Returns true if consumed.
    [[nodiscard]] bool handle_event(const SDL_Event& e);

    void render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H);

private:
    void refresh_tags();              // reload the node's current tags from the vault
    void refresh_vocabulary();        // reload the vault-wide tag vocabulary (Phase 29)
    void refresh_suggestions();       // recompute the autosuggest dropdown for the buffer
    void add_chosen_tag();            // Enter: add highlighted suggestion or typed text
    void move_cursor(int dir);        // Up/Down: suggestion highlight or tag-list scroll
    void select_tag(std::string_view tag);  // select `tag` (ci) so the list scrolls to it
    void delete_selected_tag();       // Delete: remove tally_[selected_] from every selected node

    // render() helpers (split out to keep cognitive complexity down)
    void draw_tag_rows(gfx::Renderer& r, gfx::FontAtlas& font, float mx, float list_y,
                        float tags_start, float row_pitch, int max_visible) const;
    void draw_inherited_tags(gfx::Renderer& r, gfx::FontAtlas& font, float mx, float list_bottom,
                              int shown_count, const std::vector<std::string>& lines) const;
    void draw_suggestions_dropdown(gfx::Renderer& r, gfx::FontAtlas& font, float mx,
                                    float input_y) const;

    vault::Vault&        vault_;
    gfx::Window&         win_;
    bool                       active_ = false;
    std::vector<std::string>   node_paths_;    // one or many (Phase 45 Part 2)
    std::vector<TagTallyEntry> tally_;         // union of node_paths_' tags + counts
    std::vector<std::string>   inherited_;     // read-only ancestor-gallery tags
                                                // (single-node mode only — see refresh_tags)
    std::vector<std::string> vocabulary_;    // vault-wide tags for autosuggest (Phase 29)
    std::vector<std::string> suggestions_;   // ranked matches for the typed buffer
    int                  sugg_sel_ = -1;     // -1 = editing buffer; ≥0 highlights a suggestion
    int                  selected_ = 0;      // index of the selected tag to delete
    std::string          new_tag_buf_;       // input buffer for adding a new tag
    std::string          error_;             // transient error message
};

} // namespace ui
