#include "ui/template_editor.h"

#include <algorithm>
#include <format>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/prompt_layout.h"
#include "ui/tag_inherit.h"
#include "ui/text_input_event.h"
#include "ui/widgets.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace ui {

namespace {
constexpr float PROMPT_PAD = 16.0f;
constexpr float PROMPT_INPUT_H = 32.0f;
constexpr float RADIUS = 4.0f;
}

TemplateEditAction template_edit_action(SDL_Keycode key)
{
    using enum TemplateEditAction;
    switch (key) {
        case SDLK_A:      return AddField;
        case SDLK_R:      return RenameField;
        case SDLK_DELETE: return RemoveField;
        case SDLK_ESCAPE: return Back;
        default:          return None;
    }
}

TemplateEditorPanel::TemplateEditorPanel(vault::Vault& vault, gfx::Window& win)
    : vault_(vault), win_(win)
{
}

void TemplateEditorPanel::open()
{
    active_ = true;
    stage_ = Stage::PickCategory;
    nav_.set_count(0);
    cat_name_.clear();
    field_to_rename_.clear();
    field_to_remove_.clear();
    name_buf_.clear();
    error_.clear();
    is_add_mode_ = true;

    // Load category list
    const auto& cats = vault::vault_settings(vault_).categories;
    nav_.set_count(static_cast<int>(cats.size()));
    if (!cats.empty()) nav_.select(0);
}

void TemplateEditorPanel::close()
{
    active_ = false;
    skip_text_input_ = false;
    SDL_StopTextInput(win_.sdl_window());
}

void TemplateEditorPanel::transition(Stage s)
{
    using enum Stage;
    stage_ = s;
    if (s != NameField) {
        skip_text_input_ = false;  // Clear the skip flag when leaving NameField
    }
    if (s == PickCategory || s == EditFields) {
        name_buf_.clear();
        error_.clear();
    }
}

bool TemplateEditorPanel::handle_event(const SDL_Event& e)
{
    using enum Stage;
    if (!active_) return false;

    switch (stage_) {
        case PickCategory:    return handle_event_pick_category(e);
        case EditFields:      return handle_event_edit_fields(e);
        case NameField:       return handle_event_name_field(e);
        case ConfirmRemove:   return handle_event_confirm_remove(e);
        default:                      return false;
    }
}

bool TemplateEditorPanel::handle_event_pick_category(const SDL_Event& e)
{
    if (e.type != SDL_EVENT_KEY_DOWN) return true;

    switch (e.key.key) {
        case SDLK_UP:
            nav_.move(-1);
            return true;
        case SDLK_DOWN:
            nav_.move(1);
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (const int sel = nav_.selected(); sel >= 0) {
                const auto& cats = vault::vault_settings(vault_).categories;
                if (sel >= static_cast<int>(cats.size())) return true;
                cat_name_ = cats[sel].name;
                transition(Stage::EditFields);
                // Set up field list navigation
                const auto& fields = cats[sel].fields;
                nav_.set_count(static_cast<int>(fields.size()));
                nav_.select(fields.empty() ? -1 : 0);
            }
            return true;
        case SDLK_ESCAPE:
            close();
            return true;
        default:
            return true;  // swallow all other keys
    }
}

bool TemplateEditorPanel::handle_event_edit_fields(const SDL_Event& e)
{
    if (e.type != SDL_EVENT_KEY_DOWN) return true;

    const auto& s = vault::vault_settings(vault_);
    auto tmpl = vault::category_template(s, cat_name_);

    switch (e.key.key) {
        case SDLK_UP:
            nav_.move(-1);
            return true;
        case SDLK_DOWN:
            nav_.move(1);
            return true;
        case SDLK_A:
            // Add a new field
            is_add_mode_ = true;
            field_to_rename_.clear();
            name_buf_.clear();
            transition(Stage::NameField);
            skip_text_input_ = true;  // The 'A' that opened the field also arrives as a text event
            SDL_StartTextInput(win_.sdl_window());
            return true;
        case SDLK_R:
            // Rename selected field
            if (const int sel = nav_.selected(); sel >= 0 && sel < static_cast<int>(tmpl.size())) {
                is_add_mode_ = false;
                field_to_rename_ = std::string(tmpl[sel]);
                name_buf_.set_text(field_to_rename_);
                transition(Stage::NameField);
                skip_text_input_ = true;  // The 'R' that opened the field also arrives as a text event
                SDL_StartTextInput(win_.sdl_window());
            }
            return true;
        case SDLK_DELETE:
            // Remove selected field
            if (const int sel = nav_.selected(); sel >= 0 && sel < static_cast<int>(tmpl.size())) {
                field_to_remove_ = std::string(tmpl[sel]);
                transition(Stage::ConfirmRemove);
            }
            return true;
        case SDLK_ESCAPE:
            transition(Stage::PickCategory);
            {
                const auto& cats = vault::vault_settings(vault_).categories;
                nav_.set_count(static_cast<int>(cats.size()));
                // Try to restore the previous selection
                for (int i = 0; i < static_cast<int>(cats.size()); ++i) {
                    if (cats[i].name == cat_name_) {
                        nav_.select(i);
                        break;
                    }
                }
            }
            return true;
        default:
            return true;  // swallow all other keys
    }
}

bool TemplateEditorPanel::handle_event_name_field(const SDL_Event& e)
{
    // The 'A' or 'R' that opened the field also arrives as a text event; swallow
    // exactly that one so the field does not start with 'a' or 'r' in it.
    if (e.type == SDL_EVENT_TEXT_INPUT && skip_text_input_) {
        skip_text_input_ = false;
        return true;  // Swallow this text event without inserting
    }

    // Precedence: text input first, then Enter/Esc
    if (handle_text_input_event(name_buf_, e)) return true;

    if (e.type != SDL_EVENT_KEY_DOWN) return true;

    switch (e.key.key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER: {
            const std::string new_name = name_buf_.str();
            if (new_name.empty()) {
                error_ = "Field name cannot be empty";
                return true;
            }

            auto s = vault::vault_settings(vault_);
            const auto field_tmpl = vault::category_template(s, cat_name_);
            auto fields = std::vector<std::string>(field_tmpl.begin(), field_tmpl.end());

            bool success = false;
            if (is_add_mode_) {
                // Add: check for ci duplicate
                if (const auto exists = std::ranges::find_if(fields,
                        [&new_name](const std::string& f) {
                            return tag_ci_equal(f, new_name);
                        }) != fields.end();
                    exists) {
                    error_ = "Field name already exists (case-insensitive)";
                    return true;
                }
                fields.push_back(new_name);
                success = vault::set_category_template(s, cat_name_, std::move(fields));
            } else {
                // Rename: check for ci duplicate (excluding the old name)
                if (const auto exists = std::ranges::find_if(fields,
                        [&new_name, this](const std::string& f) {
                            return !tag_ci_equal(f, field_to_rename_) && tag_ci_equal(f, new_name);
                        }) != fields.end();
                    exists) {
                    error_ = "Field name already exists (case-insensitive)";
                    return true;
                }
                success = vault::rename_template_field(s, cat_name_, field_to_rename_, new_name);
            }

            if (!success || vault::set_vault_settings(vault_, std::move(s)) != vault::VaultResult::Ok) {
                error_ = "Could not save the template";
                return true;
            }

            error_.clear();
            SDL_StopTextInput(win_.sdl_window());
            transition(Stage::EditFields);
            {
                // Reload and restore selection
                const auto& s2 = vault::vault_settings(vault_);
                auto tmpl = vault::category_template(s2, cat_name_);
                nav_.set_count(static_cast<int>(tmpl.size()));
                if (!tmpl.empty()) nav_.select(0);
            }
            return true;
        }
        case SDLK_ESCAPE:
            SDL_StopTextInput(win_.sdl_window());
            transition(Stage::EditFields);
            {
                const auto& s2 = vault::vault_settings(vault_);
                auto tmpl = vault::category_template(s2, cat_name_);
                nav_.set_count(static_cast<int>(tmpl.size()));
                if (!tmpl.empty()) nav_.select(0);
            }
            return true;
        default:
            return true;
    }
}

bool TemplateEditorPanel::handle_event_confirm_remove(const SDL_Event& e)
{
    if (e.type != SDL_EVENT_KEY_DOWN) return true;

    switch (e.key.key) {
        case SDLK_Y: {
            auto s = vault::vault_settings(vault_);
            if (!vault::remove_template_field(s, cat_name_, field_to_remove_) ||
                vault::set_vault_settings(vault_, std::move(s)) != vault::VaultResult::Ok) {
                error_ = "Could not remove the field";
                return true;  // Stay in ConfirmRemove on persist failure
            }
            error_.clear();
            transition(Stage::EditFields);
            {
                const auto& s2 = vault::vault_settings(vault_);
                auto tmpl = vault::category_template(s2, cat_name_);
                nav_.set_count(static_cast<int>(tmpl.size()));
                if (!tmpl.empty()) nav_.select(0);
            }
            return true;
        }
        case SDLK_N:
        case SDLK_ESCAPE:
            transition(Stage::EditFields);
            {
                const auto& s2 = vault::vault_settings(vault_);
                auto tmpl = vault::category_template(s2, cat_name_);
                nav_.set_count(static_cast<int>(tmpl.size()));
                if (!tmpl.empty()) nav_.select(0);
            }
            return true;
        default:
            return true;  // swallow all other keys
    }
}

void TemplateEditorPanel::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H)
{
    using enum Stage;
    if (!active_) return;

    switch (stage_) {
        case PickCategory:  render_pick_category(r, font, W, H); break;
        case EditFields:    render_edit_fields(r, font, W, H); break;
        case NameField:     render_name_field(r, font, W, H); break;
        case ConfirmRemove: render_confirm_remove(r, font, W, H); break;
    }
}

void TemplateEditorPanel::render_pick_category(gfx::Renderer& r, gfx::FontAtlas& font,
                                               float W, float H) const
{
    using namespace gfx::theme;
    const auto& cats = vault::vault_settings(vault_).categories;
    const float ph = font.pixel_height();
    const float row_h = ph + 8;

    const PromptBoxLayout l = prompt_box_layout({
        .font_px = ph,
        .window_w = W,
        .window_h = H,
        .input_h = 0.0f,
        .body_lines = std::min(static_cast<int>(cats.size()), 8),
        .body_line_h = row_h
    });

    r.draw_round_rect(l.box, RADIUS, SURFACE);
    r.draw_round_rect(l.box, RADIUS, ACCENT, /*filled*/ false);

    r.draw_text(font, l.box.x + PROMPT_PAD, l.title_y, "Edit templates", TEXT);

    float y = l.body_y;
    const int sel = nav_.selected();
    for (int i = 0; i < static_cast<int>(cats.size()); ++i) {
        const bool is_sel = (i == sel);
        if (is_sel) {
            r.draw_round_rect({l.box.x + 8, y, l.box.w - 16, row_h},
                            RADIUS / 2, ACCENT);
        }
        const auto label = std::format("{}  ({} field{})",
                                      cats[i].name,
                                      cats[i].fields.size(),
                                      cats[i].fields.size() == 1 ? "" : "s");
        r.draw_text(font, l.box.x + PROMPT_PAD + 12, y + 2, label,
                   is_sel ? SURFACE : TEXT);
        y += row_h;
    }

    if (!error_.empty()) {
        r.draw_text(font, l.box.x + PROMPT_PAD, l.hint_y, error_, DANGER);
    } else {
        r.draw_text(font, l.box.x + PROMPT_PAD, l.hint_y, "[Enter] Edit    [Esc] Close",
                   TEXT_FAINT);
    }
}

void TemplateEditorPanel::render_edit_fields(gfx::Renderer& r, gfx::FontAtlas& font,
                                             float W, float H)
{
    using namespace gfx::theme;
    const auto& s = vault::vault_settings(vault_);
    auto tmpl = vault::category_template(s, cat_name_);
    const float ph = font.pixel_height();
    const float row_h = ph + 8;

    const PromptBoxLayout l = prompt_box_layout({
        .font_px = ph,
        .window_w = W,
        .window_h = H,
        .input_h = 0.0f,
        .body_lines = std::min(static_cast<int>(tmpl.size()), 8),
        .body_line_h = row_h
    });

    r.draw_round_rect(l.box, RADIUS, SURFACE);
    r.draw_round_rect(l.box, RADIUS, ACCENT, /*filled*/ false);

    r.draw_text(font, l.box.x + PROMPT_PAD, l.title_y,
               std::format("Edit \"{}\" fields", cat_name_), TEXT);

    float y = l.body_y;
    const int sel = nav_.selected();
    for (int i = 0; i < static_cast<int>(tmpl.size()); ++i) {
        const bool is_sel = (i == sel);
        if (is_sel) {
            r.draw_round_rect({l.box.x + 8, y, l.box.w - 16, row_h},
                            RADIUS / 2, ACCENT);
        }
        r.draw_text(font, l.box.x + PROMPT_PAD + 12, y + 2, std::string_view(tmpl[i]),
                   is_sel ? SURFACE : TEXT);
        y += row_h;
    }

    if (!error_.empty()) {
        r.draw_text(font, l.box.x + PROMPT_PAD, l.hint_y, error_, DANGER);
    } else {
        r.draw_text(font, l.box.x + PROMPT_PAD, l.hint_y,
                   "[A] Add   [R] Rename   [Del] Remove   [Esc] Back", TEXT_FAINT);
    }
}

void TemplateEditorPanel::render_name_field(gfx::Renderer& r, gfx::FontAtlas& font,
                                            float W, float H)
{
    using namespace gfx::theme;
    const float ph = font.pixel_height();
    const PromptBoxLayout l = prompt_box_layout({
        .font_px = ph,
        .window_w = W,
        .window_h = H,
        .input_h = PROMPT_INPUT_H
    });

    r.draw_round_rect(l.box, RADIUS, SURFACE);
    r.draw_round_rect(l.box, RADIUS, ACCENT, /*filled*/ false);

    const std::string title = is_add_mode_ ? "Add field" : "Rename field";
    r.draw_text(font, l.box.x + PROMPT_PAD, l.title_y, title, TEXT);

    // Input field
    const float input_field_w = l.box.w - 2 * PROMPT_PAD;
    const float input_inner_w = input_field_w - 2 * 4;
    r.draw_round_rect({.x = l.box.x + PROMPT_PAD, .y = l.input_y, .w = input_field_w,
                       .h = PROMPT_INPUT_H}, RADIUS, SURFACE_HI);
    r.draw_round_rect({.x = l.box.x + PROMPT_PAD, .y = l.input_y, .w = input_field_w,
                       .h = PROMPT_INPUT_H}, RADIUS, BORDER, /*filled*/ false);

    const float text_y = l.input_y + (PROMPT_INPUT_H - ph) / 2.0f;
    draw_inline_edit_text(r, font, l.box.x + PROMPT_PAD + 4, text_y, input_inner_w,
                         name_buf_, name_chrome_);

    if (!error_.empty()) {
        r.draw_text(font, l.box.x + PROMPT_PAD, l.hint_y, error_, DANGER);
    } else {
        r.draw_text(font, l.box.x + PROMPT_PAD, l.hint_y, "[Enter] Save   [Esc] Cancel",
                   TEXT_FAINT);
    }
}

void TemplateEditorPanel::render_confirm_remove(gfx::Renderer& r, gfx::FontAtlas& font,
                                                float W, float H)
{
    using namespace gfx::theme;
    const float ph = font.pixel_height();

    const PromptBoxLayout l = prompt_box_layout({
        .font_px = ph,
        .window_w = W,
        .window_h = H,
        .input_h = 0.0f,
        .body_lines = 2,
        .body_line_h = ph + 8
    });

    r.draw_round_rect(l.box, RADIUS, SURFACE);
    r.draw_round_rect(l.box, RADIUS, ACCENT, /*filled*/ false);

    r.draw_text(font, l.box.x + PROMPT_PAD, l.title_y,
               std::format("Remove field \"{}\"?", field_to_remove_), TEXT);

    float y = l.body_y;
    r.draw_text(font, l.box.x + PROMPT_PAD, y, std::format("from \"{}\"", cat_name_), TEXT_DIM);
    y += ph + 8;
    r.draw_text(font, l.box.x + PROMPT_PAD, y, "Stored values are deleted.", TEXT_DIM);

    if (!error_.empty()) {
        r.draw_text(font, l.box.x + PROMPT_PAD, l.hint_y, error_, DANGER);
    } else {
        r.draw_text(font, l.box.x + PROMPT_PAD, l.hint_y,
                   "[Esc/N] Cancel        [Y] Remove", TEXT_FAINT);
    }
}

} // namespace ui
