#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "ui/advanced_search_model.h"
#include "ui/list_layout.h"
#include "ui/text_input_model.h"
#include "ui/widgets.h"
#include "vault/vault_search.h"

namespace gfx { class FontAtlas; class Renderer; }

namespace ui {

// Manages the saved-search sidebar of AdvancedSearchScreen: navigation (Up/Down/Enter/Del),
// naming modal (Ctrl+S then type name), loading focused search, and deleting focused search.
// Extracted to reduce AdvancedSearchScreen's method budget (cpp:S1448).
//
// The panel holds references to:
// - search_: vault::VaultSearch facade (for vault transactions)
// - font_: gfx::FontAtlas for rendering
// - status_: reference to parent screen's status_ string (shared feedback line)
//
// The panel references (but does not own):
// - saved_: public vector of vault::SavedSearch, managed by AdvancedSearchScreen via reload_saved()
//
// Session state (cur_saved) is persisted/restored by AdvancedSearchScreen via get_cursor()/set_cursor().
class SavedSearchPanel {
public:
    // Constructor takes references to:
    // - search: vault::VaultSearch facade (for save/delete operations)
    // - font: for text rendering
    // - status_ref: shared status line (transient feedback)
    // - saved_ref: the vector of SavedSearch managed by AdvancedSearchScreen
    SavedSearchPanel(vault::VaultSearch& search, gfx::FontAtlas& font, std::string& status_ref,
                     std::vector<vault::SavedSearch>& saved_ref);

    // Action signals returned by handle_key to avoid duplicate processing by the parent screen.
    enum class Action {
        None,     // no action (navigation, delete)
        Loaded,   // Enter: successfully loaded a saved search (caller should update query_)
        Deleted   // Del: deleted a saved search (caller should reload_saved())
    };

    // Accessor to the saved searches list (owned by AdvancedSearchScreen, updated via reload_saved).
    [[nodiscard]] std::vector<vault::SavedSearch>& get_saved() { return saved_; }
    [[nodiscard]] const std::vector<vault::SavedSearch>& get_saved() const { return saved_; }

    // Key navigation: Up/Down/Enter (load)/Del (delete).
    // Returns an action signal to avoid duplicate processing in the parent screen.
    Action handle_key(const SDL_KeyboardEvent& key);

    // Load the focused saved search into query_ (output via AdvancedQuery& parameter).
    // Called by Enter in the saved field. Returns true on success, false on error.
    // Caller (AdvancedSearchScreen) must then rerun() and reload_saved().
    bool load_focused(AdvancedQuery& out_query);

    // Delete the focused saved search from the vault.
    // Called by Del in the saved field. Caller must reload_saved() afterward.
    void delete_focused();

    // Enter save mode (setting_=true, clear save_buf_, ready for text input).
    // Called by Ctrl+S in AdvancedSearchScreen::handle_key().
    void begin_naming();

    // Finalize save (save the query with the typed name to vault, setting_=false).
    // Returns true on success; caller should only call reload_saved() on true.
    bool finalize_save(const AdvancedQuery& query);

    // Handle mouse wheel scrolling on the saved-search sidebar. Called when wheel event
    // is detected over the panel. max_h = window height (for scroll clamping).
    void handle_wheel(float wheel_y, float max_h);

    // Render the saved-searches sidebar (list of saved searches, focused highlight, saving modal).
    // hot = whether the Saved field is focused and not in save mode or clearing mode;
    // max_w = available width in px (long names are middle-elided to fit it);
    // max_h = window height (for scroll clamping and clipping rows outside viewport).
    void render(gfx::Renderer& r, float x, float max_w, float max_h, bool hot);

    // Session state accessors (called by AdvancedSearchScreen::on_enter/on_exit).
    int  get_cursor() const;
    void set_cursor(int cur);

    // The active text field (for event routing), or nullptr when not saving.
    // Phase 54: a full editable field — before it, this buffer had no Backspace
    // handler at all, so a typo could only be fixed by cancelling the save.
    [[nodiscard]] ITextInput* active_buffer();

    // Caret/scroll state for the save-name field, drawn by the parent screen's
    // save modal (the panel itself only draws the sidebar list).
    [[nodiscard]] TextFieldChrome& save_chrome() noexcept { return save_chrome_; }

private:
    vault::VaultSearch&               search_;  // vault facade for save/delete operations
    gfx::FontAtlas&                   font_;
    std::string&                      status_;  // reference to parent screen's transient status line
    std::vector<vault::SavedSearch>&  saved_;   // the saved searches list (owned by AdvancedSearchScreen)

    int         cur_saved_ = 0;    // selected saved search index
    float       scroll_    = 0.0f; // vertical scroll offset for the list (Phase 68 Part 3)
    float       last_max_h_ = 720.0f;  // cached window height for scroll clamping in handle_key
    bool        saving_    = false; // naming a search to save
    TextInputModel  save_buf_;     // the name being typed while saving_=true
    TextFieldChrome save_chrome_;
};

} // namespace ui
