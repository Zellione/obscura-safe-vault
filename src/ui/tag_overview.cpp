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
#include "ui/widgets.h"
#include "vault/index.h"
#include "vault/vault.h"
#include "vault/vault_search.h"

namespace ui {

namespace {
constexpr float OX  = 40;    // left margin
constexpr float OY  = 150;   // list top
constexpr float PAD = 9;     // vertical padding inside a row

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
    shown_ = filter_tags(all_, filter_);
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

void TagOverviewScreen::handle_event(const SDL_Event& e)
{
    if (quick_switch_.active()) {
        (void)quick_switch_.handle_event(e);
        if (std::string p; quick_switch_.consume_choice(p))
            request(NavKind::ToUnlock, std::move(p));   // locks current, unlocks chosen
        return;
    }

    // Handle prompt input (tag description editing)
    if (prompting_) {
        if (e.type == SDL_EVENT_TEXT_INPUT) {
            prompt_buf_ += e.text.text;
            return;
        }
        if (e.type == SDL_EVENT_KEY_DOWN) {
            switch (e.key.key) {
                case SDLK_BACKSPACE: {
                    if (!prompt_buf_.empty()) {
                        prompt_buf_.pop_back();
                    }
                    return;
                }
                case SDLK_RETURN:
                case SDLK_KP_ENTER: {
                    // Save the description
                    const int sel = nav_.selected();
                    if (sel >= 0 && sel < static_cast<int>(shown_.size())) {
                        auto s = vault::vault_settings(vault_);
                        vault::set_tag_description(s, shown_[sel].tag, prompt_buf_);
                        if (vault::set_vault_settings(vault_, std::move(s)) != vault::VaultResult::Ok) {
                            error_ = "Could not save the tag description";
                        } else {
                            error_.clear();
                            reload();
                        }
                    }
                    prompting_ = false;
                    prompt_buf_.clear();
                    SDL_StopTextInput(win_.sdl_window());
                    return;
                }
                case SDLK_ESCAPE:
                    prompting_ = false;
                    prompt_buf_.clear();
                    error_.clear();
                    SDL_StopTextInput(win_.sdl_window());
                    return;
                default:
                    break;
            }
        }
        // While prompting, swallow all other events
        return;
    }

    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            if (is_quick_switch_key(e.key)) { quick_switch_.open(); break; }   // switch vault (`)
            switch (e.key.key) {
                case SDLK_UP:       nav_.move(-1);                              break;
                case SDLK_DOWN:     nav_.move(1);                               break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER: open_selected();                           break;
                case SDLK_TAB:
                    sort_ = (sort_ == TagSort::Name) ? TagSort::Count : TagSort::Name;
                    rebuild();
                    break;
                case SDLK_E:
                    // Open prompt to edit tag description
                    if (nav_.selected() >= 0 && nav_.selected() < static_cast<int>(shown_.size())) {
                        prompting_ = true;
                        prompt_buf_ = shown_[nav_.selected()].description;
                        error_.clear();
                        SDL_StartTextInput(win_.sdl_window());
                    }
                    break;
                case SDLK_BACKSPACE:
                    if (!filter_.empty()) { filter_.pop_back(); rebuild(); }
                    else                  request(NavKind::ToGallery, "", 0);
                    break;
                case SDLK_ESCAPE:
                    if (!filter_.empty()) { filter_.clear(); rebuild(); }
                    else                  request(NavKind::ToGallery, "", 0);
                    break;
                default: break;
            }
            break;
        case SDL_EVENT_TEXT_INPUT:
            filter_ += e.text.text;
            rebuild();
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

void TagOverviewScreen::render(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());

    r.draw_text(font_, OX, 40, "Tag Overview", TEXT_DIM);
    const char* sort_label = (sort_ == TagSort::Name) ? "Sort: name (Tab)" : "Sort: count (Tab)";
    r.draw_text(font_, OX, 84, "[F1] Help", TEXT_FAINT);
    r.draw_text(font_, OX, 112, sort_label, TEXT_FAINT);
    if (!filter_.empty())
        r.draw_text(font_, OX + 280, 112,
                    fit_text(font_, "Filter: " + filter_, W - (OX + 280) - OX), TEXT);

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
        const float prompt_w = 400.0f;
        const float prompt_h = 80.0f;
        const float prompt_x = (W - prompt_w) / 2.0f;
        const float prompt_y = (H - prompt_h) / 2.0f;

        r.draw_round_rect({.x = prompt_x, .y = prompt_y, .w = prompt_w, .h = prompt_h}, RADIUS,
                         SURFACE);
        r.draw_round_rect({.x = prompt_x, .y = prompt_y, .w = prompt_w, .h = prompt_h}, RADIUS,
                         ACCENT, /*filled*/ false);

        r.draw_text(font_, prompt_x + 12.0f, prompt_y + 12.0f, "Edit tag description", TEXT);

        // Input field
        const float input_y = prompt_y + 36.0f;
        r.draw_rect({.x = prompt_x + 12.0f, .y = input_y, .w = prompt_w - 24.0f, .h = 28.0f},
                   SURFACE_HI);
        r.draw_text(font_, prompt_x + 16.0f, input_y + 4.0f,
                   prompt_buf_.empty() ? "_" : fit_text(font_, prompt_buf_, prompt_w - 32.0f),
                   TEXT);
    }
}

std::vector<HelpGroup> TagOverviewScreen::help_groups() const
{
    return {{"Navigate", {
        {"Up/Down", "Move selection"}, {"Enter", "Open tag"},
        {"Type", "Filter by prefix"}, {"Tab", "Toggle sort (name/count)"},
        {"E", "Edit tag description"},
        {"Esc", "Back"},
    }}};
}

} // namespace ui
