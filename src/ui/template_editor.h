#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ui/nav_model.h"
#include "ui/text_input_model.h"
#include "ui/widgets.h"

namespace gfx { class Window; class FontAtlas; class Renderer; }
namespace vault { class Vault; }

namespace ui {

// Pure keymap for template editor actions.
enum class TemplateEditAction : uint8_t { None, AddField, RenameField, RemoveField, Back };

// Return the action bound to a key.
[[nodiscard]] TemplateEditAction template_edit_action(SDL_Keycode key);

// Modal panel for template CRUD: PickCategory → EditFields → NameField (add|rename) /
// ConfirmRemove. Renders centered with prompt_layout. Persist-per-mutation with the
// vault_settings pattern.
class TemplateEditorPanel {
public:
    TemplateEditorPanel(vault::Vault& vault, gfx::Window& win);

    void open();
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool handle_event(const SDL_Event& e);
    void render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H);

private:
    enum class Stage : uint8_t {
        PickCategory,  // Select a category to edit
        EditFields,    // View the category's fields
        NameField,     // Enter/edit a field name (add or rename)
        ConfirmRemove  // Confirm remove of a field
    };

    void transition(Stage s);

    bool handle_event_pick_category(const SDL_Event& e);
    bool handle_event_edit_fields(const SDL_Event& e);
    bool handle_event_name_field(const SDL_Event& e);
    bool handle_event_confirm_remove(const SDL_Event& e);

    void render_pick_category(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H);
    void render_edit_fields(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H);
    void render_name_field(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H);
    void render_confirm_remove(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H);

    vault::Vault& vault_;
    gfx::Window&  win_;
    bool          active_ = false;
    bool          skip_text_input_ = false;  // Suppress the opening keypress's text event (A/R)

    Stage          stage_ = Stage::PickCategory;
    NavModel       nav_;                  // selection over categories or fields
    std::string    cat_name_;             // currently editing category
    std::string    field_to_rename_;      // for NameField(rename) case
    std::string    field_to_remove_;      // for ConfirmRemove
    bool           is_add_mode_ = true;   // NameField: true = add, false = rename
    TextInputModel name_buf_{64};         // field name input (INDEX_MAX_FIELD_BYTES)
    TextFieldChrome name_chrome_;
    std::string    error_;
};

} // namespace ui
