#include "ui/favorites_screen.h"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/vault_registry.h"
#include "ui/detail_model.h"
#include "ui/detail_panel.h"
#include "ui/grid_layout.h"
#include "ui/input.h"
#include "ui/tag_inherit.h"
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
                                 platform::VaultRegistry& registry, std::string active_path,
                                 bool initial_detail_open)
    : win_(win), font_(font), vault_(vault),
      quick_switch_(registry, std::move(active_path)), rename_(win_)
{
    detail_.panel.open = initial_detail_open;
}

bool current_detail_open(const FavoritesScreen& s) { return s.detail_.panel.open; }

void FavoritesScreen::reload()
{
    favs_ = fetch();
    nav_.set_count(static_cast<int>(favs_.size()));
    nav_.select(0);
    const auto rw = static_cast<float>(win_.width());
    cols_ = grid_columns(rw - detail_panel_width(detail_.panel.open, rw) - 2 * OX, CELL, GAP);
    detail_.key.clear();  // SearchHit::node pointers are now invalid
}

void FavoritesScreen::rebuild_detail()
{
    if (!detail_.panel.open) { return; }
    const int  idx   = nav_.selected();
    const auto count = static_cast<int>(favs_.size());

    std::string key = std::format("{}|{}", idx, count);
    if (key == detail_.key) { return; }
    detail_.key = std::move(key);
    detail_.panel.scroll = 0.0f;

    if (idx < 0 || idx >= count || favs_[static_cast<size_t>(idx)].node == nullptr) {
        detail_.content = DetailContent{};
        return;
    }
    const auto& hit = favs_[static_cast<size_t>(idx)];
    detail_.content = build_node_details(*hit.node, inherited_tags(vault_, hit.path));
}

void FavoritesScreen::on_enter()
{
    reload();
    scroll_ = 0.0f;  // reset scroll when entering
}

void FavoritesScreen::update(double)
{
    // Update scroll to keep the selected item visible.
    const int sel_idx = nav_.selected();
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());
    const auto cW = W - detail_panel_width(detail_.panel.open, W);
    if (sel_idx >= 0 && sel_idx < static_cast<int>(favs_.size())) {
        const SDL_FRect cellr = grid_cell_rect(sel_idx, grid_spec(cW, cols_));
        const float item_top = cellr.y;
        const float item_bottom = cellr.y + CELL;
        // Content height = number of rows * (cell_height + gap) - gap + top offset
        const int cols = grid_columns(cW - 2 * OX, CELL, GAP);
        const int total_rows = (static_cast<int>(favs_.size()) + cols - 1) / cols;
        const float content_height = OY + static_cast<float>(total_rows) * (CELL + GAP);
        // Apply selection-following scroll
        scroll_ = ui::ensure_visible(scroll_, item_top, item_bottom, OY, H);
        scroll_ = ui::clamp_scroll(scroll_, content_height, H);
    }

    if (std::string s; rename_.consume_completed(s)) {
        status_ = std::move(s);
        reload();   // the renamed item's new name must show up
    }

    rebuild_detail();
}

void FavoritesScreen::open_selected()
{
    const int s = nav_.selected();
    if (s < 0 || s >= static_cast<int>(favs_.size())) return;
    activate(favs_[s], s);
}

void FavoritesScreen::start_rename()
{
    if (rename_.active()) return;
    const int s = nav_.selected();
    if (s < 0 || s >= static_cast<int>(favs_.size())) return;

    const std::string& path  = favs_[s].path;
    const auto          slash = path.rfind('/');
    const std::string   gallery_path = slash == std::string::npos ? std::string{} : path.substr(0, slash);
    const std::string   name         = slash == std::string::npos ? path : path.substr(slash + 1);
    rename_.open(gallery_path, name);
}

void FavoritesScreen::handle_event(const SDL_Event& e)
{
    if (rename_.active()) { (void)rename_.handle_event(vault_, e); return; }
    if (quick_switch_.active()) {
        (void)quick_switch_.handle_event(e);
        if (std::string p; quick_switch_.consume_choice(p))
            request(NavKind::ToUnlock, std::move(p));   // locks current, unlocks chosen
        return;
    }

    using enum InputAction;
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            if (is_quick_switch_key(e.key)) { quick_switch_.open(); break; }   // switch vault (`)
            if (e.key.key == SDLK_R) { start_rename(); break; }
            if (handle_detail_panel_scroll(e.key, detail_.panel)) { break; }
            if (e.key.key == SDLK_D && (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) == 0) {
                detail_.panel.open = !detail_.panel.open;
                detail_.key.clear();
                break;
            }
            if (handle_extra_key(e.key)) break;   // subclass consumed it (e.g. tag-view toggle)
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
        case SDL_EVENT_MOUSE_WHEEL: {
            if (detail_panel_hit(detail_.panel.open, static_cast<float>(win_.width()), e.wheel.mouse_x)) {
                scroll_detail_panel(detail_.panel, e.wheel.y);
                break;
            }
            // Scroll without moving selection.
            const float scroll_step = (CELL + GAP) * 0.5f;
            scroll_ -= e.wheel.y * scroll_step;
            break;
        }
        default: break;
    }
}

int FavoritesScreen::hit_test(float mx, float my) const
{
    // Add scroll offset to mouse Y to convert from viewport to document coordinates.
    const float my_doc = my + scroll_;
    const auto W = static_cast<float>(win_.width());
    const auto cW = W - detail_panel_width(detail_.panel.open, W);
    return grid_hit(mx, my_doc, static_cast<int>(favs_.size()),
                    grid_spec(cW, cols_));
}

void FavoritesScreen::render(gfx::Renderer& r)
{
    using namespace gfx::theme;
    const auto W = static_cast<float>(win_.width());
    const auto cW = W - detail_panel_width(detail_.panel.open, W);

    r.draw_text(font_, OX, 40, title(), TEXT_DIM);
    r.draw_text(font_, OX, 90, "[F1] Help", TEXT_FAINT);

    const auto H = static_cast<float>(win_.height());
    if (favs_.empty())
        r.draw_text(font_, OX, OY, empty_hint(), TEXT_DIM);

    cols_ = grid_columns(cW - 2 * OX, CELL, GAP);
    const auto [first_idx, last_idx] = grid_visible_range(
        scroll_, CELL, GAP, OY, H, cols_, static_cast<int>(favs_.size()));
    for (int i = first_idx; i <= last_idx; ++i) {
        if (i < 0 || i >= static_cast<int>(favs_.size())) continue;
        SDL_FRect cellr = grid_cell_rect(i, grid_spec(cW, cols_));
        // Apply scroll offset to cell Y position.
        cellr.y -= scroll_;
        const bool sel = (i == nav_.selected());
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

        // Gold favorite badge, top-right (favorites screens only — tag views opt out).
        if (show_favorite_badge()) {
            const SDL_FRect badge{cellr.x + CELL - 8 - 18, cellr.y + 8, 18, 18};
            r.draw_round_rect(badge, RADIUS_SMALL, FAVORITE);
            r.draw_round_rect(badge, RADIUS_SMALL, BG, /*filled*/ false);
        }
    }

    if (!status_.empty()) r.draw_text(font_, OX, 118, status_, TEXT_FAINT);

    quick_switch_.render(r, font_, W, H);
    rename_.render(r, font_, W, H);

    if (const float pw = detail_panel_width(detail_.panel.open, W);
        pw > 0.0f) {
        const SDL_FRect panel{W - pw, 0.0f, pw, H};
        detail_.content_h = draw_detail_panel(r, font_, panel, detail_.content, detail_.panel.scroll);
        detail_.panel.scroll = ui::clamp_scroll(detail_.panel.scroll, detail_.content_h, H);
    }
}

std::vector<ui::HelpGroup> FavoritesScreen::help_groups() const
{
    std::vector<ui::HelpEntry> nav{
        {"Enter", "Open"}, {"R", "Rename"}, {"D", "Toggle the detail panel"}, {"`", "Switch vault"}, {"Esc", "Back"},
    };
    for (const auto& e : extra_help_entries()) nav.push_back(e);
    return {{"Navigate", nav}};
}

} // namespace ui
