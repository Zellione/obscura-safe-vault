#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "ui/advanced_search_model.h"
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

    // Public reference to the saved searches list (owned by AdvancedSearchScreen, updated via reload_saved).
    std::vector<vault::SavedSearch>& saved;

    // Key navigation: Up/Down/Enter (load)/Del (delete)
    void handle_key(const SDL_KeyboardEvent& key);

    // Text input while saving_=true (called by AdvancedSearchScreen::handle_text when panel is in save mode)
    void handle_text_input(const char* text);

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
    // Caller (AdvancedSearchScreen) must reload_saved() afterward.
    void finalize_save(const AdvancedQuery& query);

    // Render the saved-searches sidebar (list of saved searches, focused highlight, saving modal).
    void render(gfx::Renderer& r, float x);

    // Session state accessors (called by AdvancedSearchScreen::on_enter/on_exit).
    int  get_cursor() const;
    void set_cursor(int cur);

    // Return pointer to the active text buffer (for text input routing), or nullptr if none.
    // When saving_=true, returns &save_buf_; otherwise nullptr.
    std::string* active_buffer();

private:
    vault::VaultSearch& search_;  // vault facade for save/delete operations
    gfx::FontAtlas&     font_;
    std::string&        status_;  // reference to parent screen's transient status line

    int         cur_saved_ = 0;    // selected saved search index
    bool        saving_    = false; // naming a search to save
    std::string save_buf_;         // the name being typed while saving_=true
};

} // namespace ui
