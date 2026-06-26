#include "ui/tag_overview.h"

#include <algorithm>
#include <format>
#include <string>
#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
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
    const float row_h   = ph + 2 * PAD;
    const float bottom  = win_h - 24.0f;
    const int   visible = std::max(1, static_cast<int>((bottom - OY) / row_h));
    int         first   = 0;
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

    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            switch (e.key.key) {
                case SDLK_GRAVE:    quick_switch_.open();                       break;
                case SDLK_UP:       nav_.move(-1);                              break;
                case SDLK_DOWN:     nav_.move(1);                               break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER: open_selected();                           break;
                case SDLK_TAB:
                    sort_ = (sort_ == TagSort::Name) ? TagSort::Count : TagSort::Name;
                    rebuild();
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
    r.draw_text(font_, OX, 84,
                "[Up/Down] Move   [Enter] Open   [Type] filter   [Esc] Back", TEXT_FAINT);
    r.draw_text(font_, OX, 112, sort_label, TEXT_FAINT);
    if (!filter_.empty())
        r.draw_text(font_, OX + 280, 112, "Filter: " + filter_, TEXT);

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
    for (int i = g.first; i < g.first + g.visible && i < static_cast<int>(shown_.size()); ++i) {
        const float    y    = OY + static_cast<float>(i - g.first) * g.row_h;
        const SDL_FRect row{OX, y, W - 2 * OX, g.row_h - 4};
        const bool     sel  = (i == nav_.selected());
        if (sel) r.draw_selection_glow(row, RADIUS, ACCENT);
        r.draw_round_rect(row, RADIUS, sel ? SURFACE_HI : SURFACE);
        r.draw_round_rect(row, RADIUS, sel ? ACCENT : BORDER, /*filled*/ false);

        const float ty = y + (g.row_h - 4 - ph) * 0.5f;
        r.draw_text(font_, OX + 14, ty, shown_[i].tag, TEXT);

        const std::string counts = count_label(shown_[i].gallery_count, shown_[i].image_count);
        const float       cx     = W - OX - 14 - static_cast<float>(font_.measure(counts));
        r.draw_text(font_, cx, ty, counts, TEXT_DIM);
    }

    quick_switch_.render(r, font_, W, H);
}

} // namespace ui
