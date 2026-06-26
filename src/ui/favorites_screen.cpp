#include "ui/favorites_screen.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/vault_registry.h"
#include "ui/input.h"
#include "ui/widgets.h"

namespace ui {

namespace {
constexpr float OX = 40;
constexpr float OY = 160;
constexpr float CELL = 188;
constexpr float GAP = 16;

GridSpec grid_spec(float win_w, int cols) noexcept
{
    const float used = static_cast<float>(cols) * CELL +
                       static_cast<float>(cols > 0 ? cols - 1 : 0) * GAP;
    const float ox = std::max(OX, (win_w - used) * 0.5f);
    return {cols, CELL, GAP, ox, OY};
}
}

FavoritesScreen::FavoritesScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                                 platform::VaultRegistry& registry, std::string active_path)
    : win_(win), font_(font), vault_(vault),
      quick_switch_(registry, std::move(active_path))
{
}

void FavoritesScreen::reload()
{
    favs_ = fetch();
    nav_.set_count(static_cast<int>(favs_.size()));
    nav_.select(0);
    cols_ = grid_columns(static_cast<float>(win_.width()) - 2 * OX, CELL, GAP);
}

void FavoritesScreen::on_enter()
{
    reload();
}

void FavoritesScreen::open_selected()
{
    const int s = nav_.selected();
    if (s < 0 || s >= static_cast<int>(favs_.size())) return;
    activate(favs_[s], s);
}

void FavoritesScreen::handle_event(const SDL_Event& e)
{
    if (quick_switch_.active()) {
        (void)quick_switch_.handle_event(e);
        if (std::string p; quick_switch_.consume_choice(p))
            request(NavKind::ToUnlock, std::move(p));   // locks current, unlocks chosen
        return;
    }

    using enum InputAction;
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            if (e.key.key == SDLK_GRAVE) { quick_switch_.open(); break; }   // switch vault (`)
            switch (map_key(e.key.key, e.key.mod)) {
                case NavLeft:  nav_.move(-1);     break;
                case NavRight: nav_.move(1);      break;
                case NavUp:    nav_.move(-cols_); break;
                case NavDown:  nav_.move(cols_);  break;
                case Select:   open_selected();   break;
                case Back:     go_back();          break;
                default:       break;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (const int idx = hit_test(e.button.x, e.button.y); idx >= 0) {
                nav_.select(idx);
                open_selected();
            }
            break;
        default: break;
    }
}

int FavoritesScreen::hit_test(float mx, float my) const
{
    return grid_hit(mx, my, static_cast<int>(favs_.size()),
                    grid_spec(static_cast<float>(win_.width()), cols_));
}

void FavoritesScreen::render(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const auto W = static_cast<float>(win_.width());

    r.draw_text(font_, OX, 40, title(), TEXT_DIM);
    r.draw_text(font_, OX, 90, "[Enter] Open   [`] Switch   [Esc] Back", TEXT_FAINT);

    const auto H = static_cast<float>(win_.height());
    if (favs_.empty())
        r.draw_text(font_, OX, OY, empty_hint(), TEXT_DIM);

    cols_ = grid_columns(W - 2 * OX, CELL, GAP);   // for-loop below is a no-op when empty
    for (size_t i = 0; i < favs_.size(); ++i) {
        const SDL_FRect cellr = grid_cell_rect(static_cast<int>(i), grid_spec(W, cols_));
        const bool sel = (static_cast<int>(i) == nav_.selected());
        if (sel) r.draw_selection_glow(cellr, RADIUS, ACCENT);
        r.draw_round_rect(cellr, RADIUS, sel ? SURFACE_HI : SURFACE);
        r.draw_round_rect(cellr, RADIUS, sel ? ACCENT : BORDER, /*filled*/ false);

        const float ph      = font_.pixel_height();
        const float label_y = cellr.y + CELL - ph - 12.0f;
        draw_tile_content(r, favs_[i],
                          {cellr.x + 6, cellr.y + 6, CELL - 12, label_y - cellr.y - 12.0f});

        const std::string label = elide_middle(
            favs_[i].name, static_cast<int>(CELL - 16),
            [this](std::string_view s) { return font_.measure(s); });
        r.draw_text(font_, cellr.x + 8, label_y, label, TEXT);

        // Gold favorite badge, top-right (every tile here is a favorite).
        const SDL_FRect badge{cellr.x + CELL - 8 - 18, cellr.y + 8, 18, 18};
        r.draw_round_rect(badge, RADIUS_SMALL, FAVORITE);
        r.draw_round_rect(badge, RADIUS_SMALL, BG, /*filled*/ false);
    }

    quick_switch_.render(r, font_, W, H);
}

} // namespace ui
