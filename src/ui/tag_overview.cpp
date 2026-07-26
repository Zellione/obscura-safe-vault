#include "ui/tag_overview.h"

#include <algorithm>
#include <format>
#include <span>
#include <string>
#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/tag_chip.h"
#include "ui/tag_overview_model.h"
#include "ui/text_input_event.h"
#include "ui/widgets.h"
#include "vault/index.h"
#include "vault/vault.h"
#include "vault/vault_search.h"

namespace ui {

namespace {
constexpr float OX  = 40;    // left margin
constexpr float OY  = 150;   // list top
constexpr float PAD = 9;     // vertical padding inside a row

// Prompt sizing: scales to window width with sensible bounds
constexpr float PROMPT_WIDTH_RATIO = 0.75f;   // 75% of window width
constexpr float PROMPT_MAX_W = 900.0f;        // absolute max width
constexpr float PROMPT_MIN_W = 500.0f;        // absolute min width
constexpr float PROMPT_PAD = 16.0f;           // internal padding in prompt box
constexpr float PROMPT_TITLE_PAD = 12.0f;     // padding above title
constexpr float PROMPT_INPUT_PAD = 12.0f;     // padding above input field
constexpr float PROMPT_HINT_PAD = 8.0f;       // padding above hint line
constexpr float PROMPT_INPUT_H = 32.0f;       // height of input field
constexpr float PROMPT_LINE_H = 20.0f;        // height of title and hint lines (both use this)

// Phase 54 removed this file's private UTF-8 truncation and tail-clipping
// helpers: TextInputModel's byte cap does the former (on whole characters) and
// layout_text_field does the latter, for every field in the app rather than
// just this one.

// List geometry shared by render() + row_at() so a click lands on the row it
// looks like it lands on. `first` keeps the selection roughly centred when the
// list is taller than the viewport.
struct ListGeom {
    float row_h;
    int   visible;
    int   first;
};

ListGeom compute_geom(float ph, float win_h, int count, int sel)
{
    const float row_h      = 2 * ph + 3 * PAD;  // Two-line row: chip/counts + description
    const float bottom     = win_h - 24.0f;
    const float viewport_h = bottom - OY;
    const int   visible    = tag_overview_page_size(viewport_h, row_h);
    int         first      = 0;
    if (count > visible) first = std::clamp(sel - visible / 2, 0, count - visible);
    return {row_h, visible, first};
}

std::string count_label(int galleries, int images)
{
    return std::format("{} {}   {} {}",
                       galleries, galleries == 1 ? "gallery" : "galleries",
                       images, images == 1 ? "image" : "images");
}
} // namespace

TagOverviewScreen::TagOverviewScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                                     platform::VaultRegistry& registry, std::string active_path)
    : win_(win), font_(font), vault_(vault),
      quick_switch_(registry, std::move(active_path))
{
}

void TagOverviewScreen::on_enter()
{
    SDL_StartTextInput(win_.sdl_window());
    reload();
}

void TagOverviewScreen::on_exit()
{
    SDL_StopTextInput(win_.sdl_window());
}

void TagOverviewScreen::reload()
{
    all_ = vault::VaultSearch(vault_).tag_overview();
    rebuild();
}

void TagOverviewScreen::rebuild()
{
    shown_ = filter_tags(all_, filter_.str());
    sort_tags(shown_, sort_);
    nav_.set_count(static_cast<int>(shown_.size()));
}

void TagOverviewScreen::open_selected()
{
    const int s = nav_.selected();
    if (s < 0 || s >= static_cast<int>(shown_.size())) return;
    request(NavKind::ToTagGalleries, shown_[s].tag, 0);
}

int TagOverviewScreen::row_at(float my) const
{
    const auto g = compute_geom(font_.pixel_height(), static_cast<float>(win_.height()),
                                static_cast<int>(shown_.size()), nav_.selected());
    if (my < OY) return -1;
    const int row = g.first + static_cast<int>((my - OY) / g.row_h);
    return (row >= g.first && row < g.first + g.visible && row < static_cast<int>(shown_.size()))
               ? row : -1;
}

void TagOverviewScreen::handle_key_down_in_browse_mode(const SDL_KeyboardEvent& key)
{
    if (is_quick_switch_key(key)) { quick_switch_.open(); return; }
    switch (key.key) {
        case SDLK_UP:       nav_.move(-1);                              break;
        case SDLK_DOWN:     nav_.move(1);                               break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: open_selected();                           break;
        case SDLK_TAB:
            sort_ = (sort_ == TagSort::Name) ? TagSort::Count : TagSort::Name;
            rebuild();
            break;
        case SDLK_E:
            if (nav_.selected() >= 0 && nav_.selected() < static_cast<int>(shown_.size())) {
                prompting_ = true;
                prompt_buf_.set_text(shown_[nav_.selected()].description);
                prompt_skip_text_input_ = true;
                error_.clear();
                SDL_StartTextInput(win_.sdl_window());
            }
            break;
        case SDLK_SLASH:
            filtering_ = true;
            SDL_StartTextInput(win_.sdl_window());
            break;
        case SDLK_BACKSPACE:
        case SDLK_ESCAPE:
            request(NavKind::ToGallery, "", 0);
            break;
        default: break;
    }
}

void TagOverviewScreen::handle_event(const SDL_Event& e)
{
    if (quick_switch_.active()) {
        (void)quick_switch_.handle_event(e);
        if (std::string p; quick_switch_.consume_choice(p))
            request(NavKind::ToUnlock, std::move(p));
        return;
    }

    if (prompting_) {
        // The 'E' that opened the prompt also arrives as a text event; swallow
        // exactly that one so the field does not start with an "e" in it.
        if (e.type == SDL_EVENT_TEXT_INPUT && prompt_skip_text_input_) {
            prompt_skip_text_input_ = false;
            return;
        }
        // Precedence rule (Phase 54): the description field consumes editing
        // keys before the prompt's own Enter/Esc. The byte limit is the field's
        // own cap now, so an over-long paste truncates instead of being dropped.
        if (handle_text_input_event(prompt_buf_, e)) return;
        handle_prompt_key_event(e);
        return;
    }

    if (filtering_) { handle_filter_event(e); return; }

    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            handle_key_down_in_browse_mode(e.key);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (const int idx = row_at(e.button.y); idx >= 0) {
                nav_.select(idx);
                open_selected();
            }
            break;
        default: break;
    }
}

void TagOverviewScreen::close_filter()
{
    filter_.clear();
    filtering_ = false;
    SDL_StopTextInput(win_.sdl_window());
    rebuild();
}

void TagOverviewScreen::handle_filter_event(const SDL_Event& e)
{
    // Precedence rule (Phase 54): the filter field owns editing keys while it is
    // open. field_owns_event means a Backspace on an EMPTY filter falls through
    // below and closes filter mode instead of being silently swallowed.
    if (field_owns_event(filter_, e)) {
        const uint64_t rev = filter_.revision();
        if (handle_text_input_event(filter_, e)) {
            if (filter_.revision() != rev) rebuild();
            return;
        }
    }
    if (e.type != SDL_EVENT_KEY_DOWN) return;
    switch (e.key.key) {
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE: close_filter();  break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:  open_selected(); break;
        case SDLK_UP:        nav_.move(-1);   break;
        case SDLK_DOWN:      nav_.move(1);    break;
        default: break;
    }
}

void TagOverviewScreen::handle_prompt_key_event(const SDL_Event& e)
{
    if (e.type != SDL_EVENT_KEY_DOWN) return;

    switch (e.key.key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            // Save the description
            if (const int sel = nav_.selected(); sel >= 0 && sel < static_cast<int>(shown_.size())) {
                auto s = vault::vault_settings(vault_);
                vault::set_tag_description(s, shown_[sel].tag, prompt_buf_.str());
                if (vault::set_vault_settings(vault_, std::move(s)) != vault::VaultResult::Ok) {
                    error_ = "Could not save the tag description";
                } else {
                    error_.clear();
                    reload();
                }
            }
            prompting_ = false;
            prompt_buf_.clear();
            prompt_skip_text_input_ = false;
            SDL_StopTextInput(win_.sdl_window());
            return;
        case SDLK_ESCAPE:
            prompting_ = false;
            prompt_buf_.clear();
            prompt_skip_text_input_ = false;
            error_.clear();
            SDL_StopTextInput(win_.sdl_window());
            return;
        default:
            break;
    }
}

void TagOverviewScreen::render(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());

    r.draw_text(font_, OX, 40, "Tag Overview", TEXT_DIM);
    const char* sort_label = (sort_ == TagSort::Name) ? "Sort: name (Tab)" : "Sort: count (Tab)";
    r.draw_text(font_, OX, 84, "[F1] Help", TEXT_FAINT);
    r.draw_text(font_, OX, 112, sort_label, TEXT_FAINT);
    if (filtering_ || !filter_.empty()) {
        const float fx = OX + 280;
        const auto  lw = static_cast<float>(font_.measure("Filter: "));
        r.draw_text(font_, fx, 112, "Filter: ", TEXT);
        draw_inline_edit_text(r, font_, fx + lw, 112, W - (fx + lw) - OX,
                              filter_, filter_chrome_);
    }

    // Draw error message if present
    if (!error_.empty())
        r.draw_text(font_, OX, 132, error_, gfx::theme::DANGER);

    if (shown_.empty()) {
        r.draw_text(font_, OX, OY,
                    all_.empty() ? "No tags in this vault yet."
                                 : "No tags match the filter.",
                    TEXT_DIM);
        quick_switch_.render(r, font_, W, H);
        return;
    }

    const auto g = compute_geom(font_.pixel_height(), H, static_cast<int>(shown_.size()),
                                nav_.selected());
    const float ph = font_.pixel_height();
    // Hoisted: vault_settings returns a reference, but binding it by value here
    // would deep-copy the category vector once per visible row, every frame.
    const auto& cats = vault::vault_settings(vault_).categories;
    const float max_desc_w = W - (OX + 14) - OX - 14;  // width available for description text
    for (int i = g.first; i < g.first + g.visible && i < static_cast<int>(shown_.size()); ++i) {
        const float    y    = OY + static_cast<float>(i - g.first) * g.row_h;
        const SDL_FRect row{OX, y, W - 2 * OX, g.row_h - 4};
        const bool     sel  = (i == nav_.selected());
        if (sel) r.draw_selection_glow(row, RADIUS, ACCENT);
        r.draw_round_rect(row, RADIUS, sel ? SURFACE_HI : SURFACE);
        r.draw_round_rect(row, RADIUS, sel ? ACCENT : BORDER, /*filled*/ false);

        // Line 1: tag chip and counts
        const float ty = y + (ph - 4) * 0.5f;  // Center first line text within top half
        const std::string counts = count_label(shown_[i].gallery_count, shown_[i].image_count);
        const float       cx     = W - OX - 14 - static_cast<float>(font_.measure(counts));
        // The tag renders as a chip; the count column keeps its exact x, so the
        // two never shift relative to each other. draw_tag_chips centres its
        // content within CHIP_ROW_H, so give it the row's top, not the text top.
        draw_tag_chips(r, font_, OX + 14, y + (ph - CHIP_ROW_H) * 0.5f,
                       cx - (OX + 14) - 12, std::span(&shown_[i].tag, 1), cats);
        r.draw_text(font_, cx, ty, counts, TEXT_DIM);

        // Line 2: description (or placeholder)
        const float desc_y = y + ph + PAD;
        const std::string shown_desc = shown_[i].description.empty()
            ? std::string("(no description — [E] to add)")
            : fit_text(font_, shown_[i].description, max_desc_w);
        r.draw_text(font_, OX + 14, desc_y, shown_desc,
                   shown_[i].description.empty() ? TEXT_FAINT : TEXT_DIM);
    }

    quick_switch_.render(r, font_, W, H);

    // Draw prompt overlay if active
    if (prompting_) {
        // Size the prompt relative to the window, with bounds
        const float prompt_w = std::clamp(W * PROMPT_WIDTH_RATIO, PROMPT_MIN_W, PROMPT_MAX_W);
        const float prompt_x = (W - prompt_w) / 2.0f;

        // Prompt height: title + input field + hint line, all with padding
        const float title_h = PROMPT_LINE_H;
        const float input_h = PROMPT_INPUT_H;
        const float hint_h = PROMPT_LINE_H;
        const float prompt_h = PROMPT_TITLE_PAD + title_h + PROMPT_INPUT_PAD + input_h +
                               PROMPT_HINT_PAD + hint_h + PROMPT_PAD;
        const float prompt_y = (H - prompt_h) / 2.0f;

        // Draw prompt background and border
        r.draw_round_rect({.x = prompt_x, .y = prompt_y, .w = prompt_w, .h = prompt_h}, RADIUS,
                         SURFACE);
        r.draw_round_rect({.x = prompt_x, .y = prompt_y, .w = prompt_w, .h = prompt_h}, RADIUS,
                         ACCENT, /*filled*/ false);

        // Title
        const float title_y = prompt_y + PROMPT_TITLE_PAD;
        r.draw_text(font_, prompt_x + PROMPT_PAD, title_y, "Edit tag description", TEXT);

        // Input field
        const float input_y = title_y + title_h + PROMPT_INPUT_PAD;
        const float input_field_w = prompt_w - 2 * PROMPT_PAD;
        const float input_inner_w = input_field_w - 2 * 4;  // 4px inset on each side
        r.draw_round_rect({.x = prompt_x + PROMPT_PAD, .y = input_y, .w = input_field_w, .h = input_h},
                         RADIUS, SURFACE_HI);
        r.draw_round_rect({.x = prompt_x + PROMPT_PAD, .y = input_y, .w = input_field_w, .h = input_h},
                         RADIUS, BORDER, /*filled*/ false);

        // Draw input text with tail clipping (show what you're typing at the caret end)
        const float text_y = input_y + (input_h - font_.pixel_height()) / 2.0f;
        draw_inline_edit_text(r, font_, prompt_x + PROMPT_PAD + 4, text_y, input_inner_w,
                              prompt_buf_, prompt_chrome_);

        // Hint line: remaining bytes
        const float hint_y = input_y + input_h + PROMPT_HINT_PAD;
        const auto bytes_left = vault::INDEX_MAX_TAG_DESC_BYTES - prompt_buf_.size();
        const std::string hint = std::format("{} bytes left", bytes_left);
        r.draw_text(font_, prompt_x + PROMPT_PAD, hint_y, hint, TEXT_FAINT);
    }
}

std::vector<HelpGroup> TagOverviewScreen::help_groups() const
{
    return {{"Navigate", {
        {"Up/Down", "Move selection"}, {"Enter", "Open tag"},
        {"/", "Filter tags"}, {"Tab", "Toggle sort (name/count)"},
        {"E", "Edit description"},
        {"Esc", "Back"},
    }},
    text_editing_help_group()};
}

} // namespace ui
