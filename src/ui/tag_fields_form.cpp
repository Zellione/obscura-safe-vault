#include "ui/tag_fields_form.h"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/advanced_search_model.h"
#include "ui/tag_inherit.h"
#include "ui/tag_suggest.h"
#include "ui/text_input_event.h"
#include "ui/text_metrics.h"
#include "ui/widgets.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace ui {

namespace {
constexpr float MODAL_W = 420.0f;    // slightly smaller than TagEditor's 500
constexpr float MODAL_H = 350.0f;
constexpr float PAD = 16.0f;
constexpr float INPUT_BOX_H = 40.0f;
constexpr float SUGG_ROW = 30.0f;
}

std::string templated_new_tag_category(std::string_view tag,
                                       const std::vector<std::string>& vocabulary,
                                       const vault::VaultSettings& settings)
{
    const bool exists = std::ranges::any_of(vocabulary,
        [tag](const std::string& t) { return tag_ci_equal(t, tag); });
    if (exists) return {};
    const std::string_view prefix = vault::tag_category_prefix(tag);
    if (prefix.empty()) return {};
    for (const auto& c : settings.categories)
        if (tag_ci_equal(c.name, prefix) && !c.fields.empty()) return c.name;
    return {};
}

void TagFieldsForm::open(std::string tag, std::string category,
                         std::vector<std::string> fields, bool with_description)
{
    active_           = true;
    saved_            = false;
    with_description_ = with_description;
    tag_              = std::move(tag);
    category_         = std::move(category);
    fields_           = std::move(fields);
    error_.clear();

    const size_t rows = fields_.size() + (with_description_ ? 1 : 0);
    values_.assign(rows, {});
    accepted_.assign(rows, false);

    // Prefill from stored values / description.
    const auto& s = vault::vault_settings(vault_);
    size_t r = 0;
    if (with_description_) values_[r++] = std::string(vault::find_tag_description(s, tag_));
    for (const auto& f : fields_)
        values_[r++] = std::string(vault::find_tag_field_value(s, tag_, f));

    row_ = 0;
    load_row(0);
    SDL_StartTextInput(win_.sdl_window());
}

void TagFieldsForm::close()
{
    active_ = false;
    SDL_StopTextInput(win_.sdl_window());
}

bool TagFieldsForm::handle_event(const SDL_Event& e)
{
    if (!active_) return false;

    if (field_owns_event(buf_, e)) {
        const uint64_t rev = buf_.revision();
        if (handle_text_input_event(buf_, e)) {
            if (buf_.revision() != rev) { refresh_suggestions(); error_.clear(); }
            return true;
        }
    }
    if (e.type != SDL_EVENT_KEY_DOWN) return active_;   // modal: swallow while open

    switch (e.key.key) {
        case SDLK_ESCAPE:
            if (sugg_sel_ >= 0) { sugg_sel_ = -1; return true; }
            save_accepted_rows();
            close();
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            accept_row_and_advance();
            return true;
        case SDLK_UP:
            if (!sugg_.empty()) sugg_sel_ = move_tag_cursor(sugg_sel_, -1,
                                                static_cast<int>(sugg_.size()));
            else                move_row(-1);
            return true;
        case SDLK_DOWN:
            if (!sugg_.empty()) sugg_sel_ = move_tag_cursor(sugg_sel_, 1,
                                                static_cast<int>(sugg_.size()));
            else                move_row(1);
            return true;
        default:
            return true;   // modal swallows everything else
    }
}

void TagFieldsForm::park_current_row()
{
    values_[row_] = std::string(buf_.str());
}

void TagFieldsForm::load_row(int row)
{
    row_ = row;
    buf_.set_text(values_[row_]);
    refresh_suggestions();
}

void TagFieldsForm::refresh_suggestions()
{
    sugg_ = description_row(row_) ? std::vector<std::string>{} :
            field_value_suggestions(buf_.str(), category_, field_name(row_),
                                    vault::vault_settings(vault_));
    sugg_sel_ = -1;
}

void TagFieldsForm::move_row(int dir)
{
    park_current_row();
    row_ = std::clamp(row_ + dir, 0, static_cast<int>(values_.size()) - 1);
    load_row(row_);
}

std::string_view TagFieldsForm::field_name(int row) const
{
    const int field_offset = with_description_ ? 1 : 0;
    if (row < field_offset) return {};
    const int idx = row - field_offset;
    if (idx >= static_cast<int>(fields_.size())) return {};
    return fields_[idx];
}

void TagFieldsForm::accept_row_and_advance()
{
    const bool from_sugg = sugg_sel_ >= 0 && sugg_sel_ < static_cast<int>(sugg_.size());
    values_[row_]   = from_sugg ? sugg_[sugg_sel_] : std::string(buf_.str());
    accepted_[row_] = true;
    if (row_ + 1 >= static_cast<int>(values_.size())) {
        save_accepted_rows();
        close();
        return;
    }
    ++row_;
    load_row(row_);
}

void TagFieldsForm::save_accepted_rows()
{
    bool any = false;
    for (const bool a : accepted_) any = any || a;
    if (!any) return;

    auto s = vault::vault_settings(vault_);
    size_t r = 0;
    if (with_description_) {
        if (accepted_[r]) vault::set_tag_description(s, tag_, values_[r]);
        ++r;
    }
    for (const auto& f : fields_) {
        if (accepted_[r]) vault::set_tag_field_value(s, tag_, f, values_[r]);
        ++r;
    }
    if (vault::set_vault_settings(vault_, std::move(s)) != vault::VaultResult::Ok) {
        error_ = "Could not save tag fields";
        return;
    }
    saved_ = true;
}

bool TagFieldsForm::consume_saved()
{
    const bool result = saved_;
    saved_ = false;
    return result;
}

void TagFieldsForm::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H)
{
    if (!active_) return;

    using namespace gfx::theme;
    const float LINE      = line_pitch(font.pixel_height());

    // Modal panel background (no veil, we're hosted inside TagEditor's already-dimmed modal)
    const float mx = (W - MODAL_W) / 2;
    const float my = (H - MODAL_H) / 2;
    r.draw_round_rect({mx, my, MODAL_W, MODAL_H}, RADIUS_SMALL, SURFACE);
    r.draw_round_rect({mx, my, MODAL_W, MODAL_H}, RADIUS_SMALL, ACCENT, /*filled*/ false);

    // Title: "tag — field-name (1/3)"
    const std::string_view field_label = description_row(row_) ? "description" : field_name(row_);
    const std::string title = std::format("{} — {} ({}/{})", tag_, field_label, row_ + 1, values_.size());
    r.draw_text(font, mx + PAD, my + PAD, title, TEXT);

    // Input box for this row's value
    const float input_y = my + PAD + LINE + 8;
    const SDL_FRect input_box{mx + PAD, input_y, MODAL_W - 2 * PAD, INPUT_BOX_H};
    r.draw_round_rect(input_box, RADIUS_SMALL, SURFACE);
    r.draw_round_rect(input_box, RADIUS_SMALL, ACCENT, /*filled*/ false);
    draw_inline_edit_text(r, font, input_box.x + 8,
                          font.text_top_for_center(input_box.y + input_box.h * 0.5f),
                          input_box.w - 16, buf_, chrome_);

    // Dropdown rows (plain text, not tag chips)
    if (!sugg_.empty()) {
        const float drop_top = input_y + INPUT_BOX_H + 4;
        const float drop_h = SUGG_ROW * static_cast<float>(sugg_.size()) + 8;
        const SDL_FRect drop{mx + PAD, drop_top, MODAL_W - 2 * PAD, drop_h};
        r.draw_round_rect(drop, RADIUS_SMALL, SURFACE_HI);
        r.draw_round_rect(drop, RADIUS_SMALL, ACCENT, /*filled*/ false);

        for (int i = 0; i < static_cast<int>(sugg_.size()); ++i) {
            const bool sel = i == sugg_sel_;
            const float row_y = drop_top + 4 + SUGG_ROW * static_cast<float>(i);
            const float ty = font.text_top_for_center(row_y + SUGG_ROW * 0.5f);

            // Draw the marker (> or space)
            const std::string marker = sel ? ">" : " ";
            const gfx::Color marker_color = sel ? TEXT : TEXT_DIM;
            r.draw_text(font, drop.x + 10, ty, marker, marker_color);

            // Draw the suggestion as plain text
            const auto text_x = drop.x + 10 + static_cast<float>(font.measure("> "));
            const auto max_w = drop.w - (text_x - drop.x) - 10;
            const auto shown = fit_text(font, sugg_[i], static_cast<int>(max_w));
            r.draw_text(font, text_x, ty, shown, sel ? TEXT : TEXT_DIM);
        }
    }

    // Footer area: error message + hint line
    const float footer_y = my + MODAL_H - 2 * LINE - PAD;
    if (!error_.empty()) {
        r.draw_text(font, mx + PAD, footer_y, error_, DANGER);
    }

    // Hint line (one pitch above the bottom)
    const float hint_y = my + MODAL_H - LINE - PAD;
    r.draw_text(font, mx + PAD, hint_y, "[Enter] Next/Save   [Esc] Skip", TEXT_FAINT);
}

} // namespace ui
