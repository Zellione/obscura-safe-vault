#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <string_view>
#include <vector>

#include "ui/text_input_model.h"
#include "ui/widgets.h"
#include "vault/index.h"

namespace gfx { class Renderer; class FontAtlas; class Window; }
namespace vault { class Vault; }

namespace ui {

// The name of the category whose template should prompt for `tag`, or empty:
// non-empty exactly when `tag` is absent from `vocabulary` (ci) AND its prefix
// names a configured category with a non-empty template (Phase 73). Pure.
[[nodiscard]] std::string templated_new_tag_category(
    std::string_view tag, const std::vector<std::string>& vocabulary,
    const vault::VaultSettings& settings);

// Modal wizard/form for a tag's template-field values (Phase 73), used by the
// tag editor (after a brand-new templated tag is added) and the tag overview's
// E prompt (with_description = true adds a description row on top). One row is
// active at a time; Enter accepts (highlighted dropdown suggestion wins over
// typed text) and advances, Enter on the last row saves everything in ONE
// set_vault_settings commit; Esc closes keeping only rows accepted so far.
// The sheet never gates the tag add — it only collects values.
class TagFieldsForm {
public:
    TagFieldsForm(vault::Vault& vault, gfx::Window& win) : vault_(vault), win_(win) {}

    void open(std::string tag, std::string category, std::vector<std::string> fields,
              bool with_description);
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool handle_event(const SDL_Event& e);
    void render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H);
    [[nodiscard]] bool consume_saved();   // true once after a save

private:
    void park_current_row();            // buffer → values_[row_]
    void load_row(int row);             // values_[row] → buffer
    void accept_row_and_advance();      // Enter
    void save_accepted_rows();          // ONE set_vault_settings
    void refresh_suggestions();
    void move_row(int dir);
    [[nodiscard]] bool description_row(int row) const noexcept
    {
        return with_description_ && row == 0;
    }
    [[nodiscard]] std::string_view field_name(int row) const;   // rows_ label sans desc offset

    vault::Vault& vault_;
    gfx::Window&  win_;
    bool          active_ = false;
    bool          saved_  = false;
    bool          with_description_ = false;
    std::string   tag_;
    std::string   category_;
    std::vector<std::string> fields_;      // template order
    std::vector<std::string> values_;      // parallel to rows (desc first if present)
    std::vector<bool>        accepted_;    // row explicitly accepted with Enter
    int                      row_ = 0;
    TextInputModel           buf_;
    TextFieldChrome          chrome_;
    std::vector<std::string> sugg_;
    int                      sugg_sel_ = -1;
    std::string              error_;
};

} // namespace ui
