#include "ui/import_tag_dialog.h"
#include <algorithm>
#include <format>
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/tag_inherit.h"
#include "ui/text_input_event.h"
#include "ui/text_metrics.h"
#include "vault/vault.h"
#include "vault/vault_search.h"

namespace ui {
namespace {
bool matches(std::string_view text, std::string_view query)
{
    if (query.empty()) return true;
    return std::search(text.begin(), text.end(), query.begin(), query.end(),
        [](char a, char b) { return tag_ci_equal(std::string_view(&a, 1), std::string_view(&b, 1)); }) != text.end();
}
}
std::vector<std::string> import_tag_vocabulary(vault::Vault& vault)
{
    auto tags = vault::VaultSearch(vault).all_tags();
    const auto settings = vault::vault_settings(vault);
    auto add = [&](std::string_view tag) {
        if (std::ranges::none_of(tags, [&](const auto& t) { return tag_ci_equal(t, tag); }))
            tags.emplace_back(tag);
    };
    for (const auto& d : settings.tag_descriptions) add(d.tag.view());
    for (const auto& f : settings.tag_field_values) add(f.tag.view());
    std::ranges::sort(tags);
    return tags;
}
void ImportTagDialog::open(ImportTagReview review, std::string title)
{
    close();
    review_ = std::move(review);
    title_ = std::move(title);
    restore_text_input_ = SDL_TextInputActive(window_.sdl_window());
    SDL_StopTextInput(window_.sdl_window());
    active_ = true;
    rebuild();
}
void ImportTagDialog::close()
{
    if (active_) {
        SDL_StopTextInput(window_.sdl_window());
        if (restore_text_input_) SDL_StartTextInput(window_.sdl_window());
    }
    restore_text_input_ = false;
    skip_opening_text_ = false;
    active_ = false;
    review_ = {};
    result_.reset();
    mode_ = Mode::Browse;
    input_.clear();
    title_.clear();
    filter_.clear();
    error_.clear();
    visible_.clear();
    selected_ = 0;
}
std::optional<ImportTagReview> ImportTagDialog::take_result()
{
    auto out = std::move(result_);
    result_.reset();
    return out;
}
void ImportTagDialog::rebuild()
{
    visible_.clear();
    if (mode_ == Mode::Replace) {
        for (size_t i = 0; i < review_.existing().size(); ++i)
            if (matches(review_.existing()[i], input_.view())) visible_.push_back(i);
    } else {
        for (size_t i = 0; i < review_.rows().size(); ++i)
            if (matches(review_.rows()[i].source, filter_) || matches(review_.target(i), filter_))
                visible_.push_back(i);
    }
    selected_ = std::clamp(selected_, 0, std::max(0, static_cast<int>(visible_.size()) - 1));
}
void ImportTagDialog::select_row(int delta)
{
    selected_ = std::clamp(selected_ + delta, 0, std::max(0, static_cast<int>(visible_.size()) - 1));
}
void ImportTagDialog::edit(Mode mode)
{
    if (visible_.empty()) return;
    editing_ = visible_[static_cast<size_t>(selected_)];
    mode_ = mode;
    skip_opening_text_ = true;
    input_.set_text(mode == Mode::Rename ? review_.target(editing_) : "");
    input_.select_all();
    if (mode == Mode::Replace) { selected_ = 0; rebuild(); }
    SDL_StartTextInput(window_.sdl_window());
}
void ImportTagDialog::accept_edit()
{
    bool accepted = true;
    if (mode_ == Mode::Rename) accepted = review_.rename(editing_, input_.view());
    if (mode_ == Mode::Replace) {
        accepted = !visible_.empty() && review_.replace(editing_,
            review_.existing()[visible_[static_cast<size_t>(selected_)]]);
    }
    if (!accepted) { error_ = "Choose a valid, non-empty tag."; return; }
    mode_ = Mode::Browse;
    error_.clear();
    SDL_StopTextInput(window_.sdl_window());
    rebuild();
}
void ImportTagDialog::browse_key(SDL_Keycode key)
{
    switch (key) {
    case SDLK_ESCAPE: close(); break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: {
        auto accepted = std::move(review_);
        close();
        result_ = std::move(accepted);
        break;
    }
    case SDLK_SPACE:
        if (!visible_.empty()) {
            auto& row = review_.rows()[visible_[static_cast<size_t>(selected_)]];
            row.selected = !row.selected;
        }
        break;
    case SDLK_A: for (const auto i : visible_) review_.rows()[i].selected = true; break;
    case SDLK_N: for (const auto i : visible_) review_.rows()[i].selected = false; break;
    case SDLK_C: review_.set_cleanup(!review_.cleanup()); rebuild(); break;
    case SDLK_R: edit(Mode::Rename); break;
    case SDLK_E: edit(Mode::Replace); break;
    case SDLK_SLASH:
        mode_ = Mode::Filter;
        skip_opening_text_ = true;
        input_.set_text(filter_);
        SDL_StartTextInput(window_.sdl_window());
        break;
    default: break;
    }
}
void ImportTagDialog::handle_event(const SDL_Event& e)
{
    if (!active_) return;
    if (skip_opening_text_ && e.type == SDL_EVENT_TEXT_INPUT) {
        skip_opening_text_ = false;
        return;
    }
    if (e.type == SDL_EVENT_KEY_UP || e.type == SDL_EVENT_KEY_DOWN) skip_opening_text_ = false;
    if (mode_ != Mode::Browse && handle_text_input_event(input_, e)) {
        if (mode_ == Mode::Filter) filter_ = input_.str();
        if (mode_ != Mode::Rename) rebuild();
        return;
    }
    if (e.type == SDL_EVENT_MOUSE_WHEEL || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        handle_pointer(e);
        return;
    }
    if (e.type == SDL_EVENT_KEY_DOWN) handle_key(e.key.key);
}
void ImportTagDialog::handle_pointer(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_MOUSE_WHEEL) { select_row(e.wheel.y > 0 ? -3 : 3); return; }
    if (e.button.button != SDL_BUTTON_LEFT) return;
    const SDL_FPoint p{e.button.x, e.button.y};
    if (!SDL_PointInRectFloat(&p, &list_box_)) return;
    selected_ = std::min(first_ + static_cast<int>((p.y - list_box_.y) / pitch_),
                         std::max(0, static_cast<int>(visible_.size()) - 1));
    if (mode_ == Mode::Browse) browse_key(SDLK_SPACE);
}
void ImportTagDialog::handle_key(SDL_Keycode key)
{
    switch (key) {
    case SDLK_UP: select_row(-1); return;
    case SDLK_DOWN: select_row(1); return;
    case SDLK_PAGEUP: select_row(-10); return;
    case SDLK_PAGEDOWN: select_row(10); return;
    default: break;
    }
    if (mode_ == Mode::Browse) { browse_key(key); return; }
    if (key == SDLK_ESCAPE) {
        mode_ = Mode::Browse;
        error_.clear();
        SDL_StopTextInput(window_.sdl_window());
        rebuild();
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) accept_edit();
}

void ImportTagDialog::render_rows(gfx::Renderer& r, gfx::FontAtlas& font)
{
    using namespace gfx::theme;
    const int count = static_cast<int>(list_box_.h / pitch_);
    if (count < 1) return;
    first_ = std::max(0, selected_ - count + 1);
    for (int j = first_; j < std::min(first_ + count, static_cast<int>(visible_.size())); ++j) {
        const size_t i = visible_[static_cast<size_t>(j)];
        const float at = list_box_.y + static_cast<float>(j - first_) * pitch_;
        if (j == selected_) r.draw_round_rect({list_box_.x, at, list_box_.w, pitch_}, RADIUS_SMALL, BG);
        const auto color = j == selected_ ? ACCENT : TEXT;
        if (mode_ == Mode::Replace) { r.draw_text(font, list_box_.x, at, fit_text_tail(font, review_.existing()[i], list_box_.w), color); continue; }
        const float column = (list_box_.w - 24) / 2;
        const auto source = std::string(review_.rows()[i].selected ? "[x] " : "[ ] ") + review_.rows()[i].source;
        const auto target = review_.target(i);
        r.draw_text(font, list_box_.x, at, fit_text_tail(font, source, column), color);
        r.draw_text(font, list_box_.x + column + 24, at,
                    fit_text_tail(font, target.empty() ? "(skip: empty)" : target, column), color);
    }
}
ImportTagDialogLayout import_tag_dialog_layout(float font_px, float width, float height)
{
    const float pitch = line_pitch(font_px);
    const float w = std::max(1.0f, std::min(width - 24, 1100.0f));
    const float h = std::max(1.0f, std::min(height - 24, pitch * 20));
    const float x = (width - w) / 2;
    const float y = (height - h) / 2;
    const float list_height = std::max(0.0f, h - 8 * pitch - 24);
    return {{x, y, w, h}, {x + 16, y + 12 + 3 * pitch, std::max(1.0f, w - 32), list_height},
            pitch, y + h - 5 * pitch - 12};
}
void ImportTagDialog::render(gfx::Renderer& r, gfx::FontAtlas& font, float width, float height)
{
    if (!active_) return;
    using namespace gfx::theme;
    const auto layout = import_tag_dialog_layout(font.pixel_height(), width, height);
    pitch_ = layout.pitch;
    const auto [x, y, w, h] = layout.box;
    const float inner = w - 32;
    r.draw_rect({0, 0, width, height}, MODAL_VEIL);
    r.draw_round_rect({x, y, w, h}, RADIUS, SURFACE);
    r.draw_round_rect({x, y, w, h}, RADIUS, ACCENT, false);
    auto line = [&](std::string_view text, float at, gfx::Color color) {
        r.draw_text(font, x + 16, at, fit_text_tail(font, text, inner), color);
    };
    line(title_, y + 12, TEXT);
    line(std::format("[C] Remove <...> and (...): {}    {} source tags    Filter: {}",
                     review_.cleanup() ? "ON" : "OFF", review_.rows().size(), filter_), y + 12 + pitch_, TEXT_DIM);
    line("Source tag  ->  Imported tag (empty results are skipped)", y + 12 + 2 * pitch_, TEXT_DIM);
    list_box_ = layout.list;
    render_rows(r, font);
    const float at = layout.controls_y;
    if (mode_ != Mode::Browse) {
        std::string_view caption = "Filter source / imported tags:";
        if (mode_ == Mode::Replace) caption = "Choose existing tag (type to search, Up/Down, Enter):";
        if (mode_ == Mode::Rename) caption = "Rename to (include category: if needed):";
        line(caption, at, TEXT);
        draw_edit_field(r, font, {x + 16, at + pitch_, inner, pitch_ + 8}, input_, chrome_, true);
    } else {
        line("[Space/click] Include  [A] All shown  [N] None shown  [/] Filter", at, TEXT_DIM);
        line("[R] Rename  [E] Replace with existing  [Up/Down/PgUp/PgDn] Navigate", at + pitch_, TEXT_DIM);
    }
    line(error_, y + h - 2 * pitch_ - 8, DANGER);
    line(mode_ == Mode::Browse ? "[Enter] Import selected tags    [Esc] Skip tag import (media is kept)"
                               : "[Enter] Accept edit    [Esc] Back to review", y + h - pitch_ - 8, TEXT_DIM);
}
} // namespace ui
