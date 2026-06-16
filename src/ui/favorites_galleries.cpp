#include "ui/favorites_galleries.h"

#include <string>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
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

FavoritesGalleries::FavoritesGalleries(gfx::Window& win, gfx::FontAtlas& font,
                                       vault::Vault& vault)
    : win_(win), font_(font), vault_(vault)
{
}

void FavoritesGalleries::on_enter()
{
    favs_ = vault_.list_favorite_galleries();
    nav_.set_count(static_cast<int>(favs_.size()));
    nav_.select(0);
    cols_ = grid_columns(static_cast<float>(win_.width()) - 2 * OX, CELL, GAP);
}

void FavoritesGalleries::open_selected()
{
    const int s = nav_.selected();
    if (s < 0 || s >= static_cast<int>(favs_.size())) return;
    request(NavKind::ToGallery, favs_[s].path, 0);
}

void FavoritesGalleries::handle_event(const SDL_Event& e)
{
    using enum InputAction;
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            switch (map_key(e.key.key, e.key.mod)) {
                case NavLeft:  nav_.move(-1);     break;
                case NavRight: nav_.move(1);      break;
                case NavUp:    nav_.move(-cols_); break;
                case NavDown:  nav_.move(cols_);  break;
                case Select:   open_selected();   break;
                case Back:     request(NavKind::ToGallery, "", 0); break;
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

int FavoritesGalleries::hit_test(float mx, float my) const
{
    return grid_hit(mx, my, static_cast<int>(favs_.size()),
                    grid_spec(static_cast<float>(win_.width()), cols_));
}

void FavoritesGalleries::render(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const auto W = static_cast<float>(win_.width());

    r.draw_text(font_, OX, 40, "Favorite Galleries", TEXT_DIM);
    r.draw_text(font_, OX, 90, "[Enter] Open   [Esc] Back", TEXT_FAINT);

    if (favs_.empty()) {
        r.draw_text(font_, OX, OY, "No favorite galleries yet. Press [B] on a "
                    "gallery tile to bookmark it.", TEXT_DIM);
        return;
    }

    cols_ = grid_columns(W - 2 * OX, CELL, GAP);
    for (size_t i = 0; i < favs_.size(); ++i) {
        const SDL_FRect cellr = grid_cell_rect(static_cast<int>(i), grid_spec(W, cols_));
        const bool sel = (static_cast<int>(i) == nav_.selected());
        if (sel) r.draw_selection_glow(cellr, RADIUS, ACCENT);
        r.draw_round_rect(cellr, RADIUS, sel ? SURFACE_HI : SURFACE);
        r.draw_round_rect(cellr, RADIUS, sel ? ACCENT : BORDER, /*filled*/ false);

        // Folder glyph (matches the gallery grid's gallery tiles).
        const float ph      = font_.pixel_height();
        const float label_y = cellr.y + CELL - ph - 12.0f;
        const SDL_FRect box{cellr.x + 6, cellr.y + 6, CELL - 12, label_y - cellr.y - 12.0f};
        const float ix = box.w * 0.18f;
        r.draw_round_rect({box.x + ix, box.y + box.h * 0.28f,
                           box.w - 2 * ix, box.h * 0.48f}, RADIUS_SMALL, FOLDER);

        const std::string label = elide_middle(
            favs_[i].name, static_cast<int>(CELL - 16),
            [this](std::string_view s) { return font_.measure(s); });
        r.draw_text(font_, cellr.x + 8, label_y, label, TEXT);

        // Gold favorite badge, top-right (every tile here is a favorite).
        const SDL_FRect badge{cellr.x + CELL - 8 - 18, cellr.y + 8, 18, 18};
        r.draw_round_rect(badge, RADIUS_SMALL, FAVORITE);
        r.draw_round_rect(badge, RADIUS_SMALL, BG, /*filled*/ false);
    }
}

} // namespace ui
