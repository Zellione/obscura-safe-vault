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
#include "ui/favorite_batch.h"
#include "ui/grid_layout.h"
#include "ui/input.h"
#include "ui/parent_group.h"
#include "ui/position_label.h"
#include "ui/tag_inherit.h"
#include "ui/widgets.h"
#include "vault/vault.h"

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
                                 platform::VaultRegistry& registry, const CollectionOps& ops)
    : win_(win), font_(font), vault_(vault),
      quick_switch_(registry, ops.active_path), rename_(win_),
      ops_(CollectionBatchOps::Deps{vault, ops.active_path, registry, ops.file_dialog,
                                    win, ops.second, ops.folder_dialog, ops.queue})
{
}

bool current_detail_open(const FavoritesScreen& s) { return s.detail_.panel.open; }

void FavoritesScreen::reload()
{
    favs_ = fetch();
    nav_.set_count(static_cast<int>(favs_.size()));
    nav_.select(0);
    sel_.clear();   // selection indices are only valid against the old listing
    const auto rw = static_cast<float>(win_.width());
    cols_ = grid_columns(rw - detail_panel_width(detail_.panel.open, rw) - 2 * OX, CELL, GAP);
    detail_.key.clear();  // SearchHit::node pointers are now invalid
}

void FavoritesScreen::rebuild_detail()
{
    if (!detail_.panel.open) { return; }
    const int  idx   = nav_.selected();
    const auto count = static_cast<int>(favs_.size());

    std::string key = std::format("{}|{}|{}", idx, count, sel_.revision());
    if (key == detail_.key) { return; }
    detail_.key = std::move(key);
    detail_.panel.scroll = 0.0f;

    // 2+ selected: the Phase 48 aggregate summary. Collection hits are not
    // siblings, so there is no single inherited cascade — pass none.
    if (sel_.count() >= 2) {
        std::vector<const vault::IndexNode*> nodes;
        for (int i : sel_.indices()) {
            if (i >= 0 && i < count) { nodes.push_back(favs_[static_cast<size_t>(i)].node); }
        }
        detail_.content = build_selection_details(nodes, {});
        return;
    }

    if (idx < 0 || idx >= count || favs_[static_cast<size_t>(idx)].node == nullptr) {
        detail_.content = DetailContent{};
        return;
    }
    const auto& hit = favs_[static_cast<size_t>(idx)];
    const auto from_contents = hit.node->is_gallery() ? contents_tags(vault_, hit.path) : std::vector<std::string>{};
    detail_.content = build_node_details(*hit.node, inherited_tags(vault_, hit.path), from_contents, vault::vault_settings(vault_).default_sort);
}

void FavoritesScreen::on_enter()
{
    reload();
    scroll_ = 0.0f;  // reset scroll when entering
}

void FavoritesScreen::on_vault_changed()
{
    // Phase 50: vault's index tree changed (background import drain attached nodes).
    // favs_ pointers are now stale; re-fetch.
    reload();
    mark_dirty();
}

void FavoritesScreen::update(double)
{
    if (auto p = ops_.poll(); p.dirty || p.reload || !p.status.empty()) {
        if (!p.status.empty()) { status_ = std::move(p.status); }
        if (p.reload) { reload(); }   // a Move removed items from the collection
        mark_dirty();
    }
    // While a batch worker owns the vault handle, the tree must not be walked
    // (no detail rebuild) — only the polling above and the modal render run.
    if (batch_ops_busy()) { return; }

    // Clamp every frame (content/viewport can change under the scroll), but
    // FOLLOW the selection only when key navigation just moved it — following
    // unconditionally fights the mouse wheel, which scrolls without moving the
    // selection, snapping the view back each frame (jitter, and a hard wall at
    // the selection's visibility window). Same fix as GalleryGrid's ScrollFollow.
    const int sel_idx = nav_.selected();
    const auto W = static_cast<float>(win_.width());
    const auto H = static_cast<float>(win_.height());
    const auto cW = W - detail_panel_width(detail_.panel.open, W);
    // Content height = number of rows * (cell_height + gap) - gap + top offset
    const int cols = grid_columns(cW - 2 * OX, CELL, GAP);
    const int total_rows = (static_cast<int>(favs_.size()) + cols - 1) / cols;
    const float content_height = OY + static_cast<float>(total_rows) * (CELL + GAP);
    if (follow_scroll_ && sel_idx >= 0 && sel_idx < static_cast<int>(favs_.size())) {
        const SDL_FRect cellr = grid_cell_rect(sel_idx, grid_spec(cW, cols_));
        scroll_ = ui::ensure_visible(scroll_, cellr.y, cellr.y + CELL, OY, H);
    }
    follow_scroll_ = false;
    scroll_ = ui::clamp_scroll(scroll_, content_height, H);

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

// --- Phase 68: batch operations over the Space/Ctrl+A selection -------------

// Mirrors the grid's Ctrl+A: select every tile, EXCEPT when everything is
// already selected — then clear (the same key undoes itself).
void FavoritesScreen::toggle_select_all()
{
    const auto count = static_cast<int>(favs_.size());
    if (count == 0) { return; }
    if (sel_.all_selected(count)) {
        sel_.clear();
    } else {
        sel_.select_all(count);
    }
}

// B: batch favorite over the selection (grid rule — any unfavorited => favorite
// all, else unfavorite all; one index commit). Falls back to the focused tile.
// On the favorites screens an unfavorited tile disappears, so reload after.
void FavoritesScreen::toggle_favorite_batch()
{
    std::vector<const vault::IndexNode*> nodes;
    std::vector<std::string>             paths;
    if (sel_.empty()) {
        const int s = nav_.selected();
        if (s < 0 || s >= static_cast<int>(favs_.size())) { return; }
        nodes.push_back(favs_[static_cast<size_t>(s)].node);
        paths.push_back(favs_[static_cast<size_t>(s)].path);
    } else {
        for (int i : sel_.indices()) {
            if (i < 0 || i >= static_cast<int>(favs_.size())) { continue; }
            nodes.push_back(favs_[static_cast<size_t>(i)].node);
            paths.push_back(favs_[static_cast<size_t>(i)].path);
        }
    }
    if (paths.empty()) { return; }
    const bool target = batch_favorite_target(nodes);
    (void)vault::set_favorites_batch(vault_, paths, target);
    status_ = std::format("{} {} {}", target ? "Favorited" : "Unfavorited", paths.size(),
                          paths.size() == 1 ? "item" : "items");
    reload();
    mark_dirty();
}

// X: consent-gated export of the selection (Phase 10 invariant deviation — the
// consent modal + selection-only scope are the mitigation, exactly as on the grid).
void FavoritesScreen::start_export()
{
    // The collect callback resolves the selection when the folder pick lands —
    // favs_ may have been reloaded by a background import drain in between,
    // which clears sel_ and turns the export into a clean no-op.
    ops_.request_export(sel_.count(), [this]() {
        std::vector<const vault::IndexNode*> picked;
        for (int i : sel_.indices()) {
            if (i >= 0 && i < static_cast<int>(favs_.size()) &&
                favs_[static_cast<size_t>(i)].node != nullptr) {
                picked.push_back(favs_[static_cast<size_t>(i)].node);
            }
        }
        return picked;
    }, status_);
}

// M: move/copy the selection. Collection screens list items from DIFFERENT
// parent galleries, so media are grouped per parent (group_by_parent) and
// gallery hits travel as whole subtrees — one dialog run for all of it.
void FavoritesScreen::start_transfer()
{
    std::vector<std::string> media_paths;
    std::vector<std::string> gallery_paths;
    if (sel_.empty()) {
        const int s = nav_.selected();
        if (s < 0 || s >= static_cast<int>(favs_.size())) { return; }
        const auto& hit = favs_[static_cast<size_t>(s)];
        (hit.is_gallery ? gallery_paths : media_paths).push_back(hit.path);
    } else {
        for (int i : sel_.indices()) {
            if (i < 0 || i >= static_cast<int>(favs_.size())) { continue; }
            const auto& hit = favs_[static_cast<size_t>(i)];
            (hit.is_gallery ? gallery_paths : media_paths).push_back(hit.path);
        }
    }
    if (media_paths.empty() && gallery_paths.empty()) { return; }
    ops_.request_transfer(group_by_parent(media_paths), std::move(gallery_paths), status_);
    mark_dirty();
}

// Del: batch delete the selection (Phase 74) — focused row when empty. The
// aggregate confirm modal, queue exclusivity, and the job run live in ops_.
void FavoritesScreen::start_delete_selection()
{
    std::vector<std::string> paths;
    if (sel_.empty()) {
        const int s = nav_.selected();
        if (s < 0 || s >= static_cast<int>(favs_.size())) { return; }
        paths.push_back(favs_[static_cast<size_t>(s)].path);
    } else {
        for (int i : sel_.indices()) {
            if (i < 0 || i >= static_cast<int>(favs_.size())) { continue; }
            paths.push_back(favs_[static_cast<size_t>(i)].path);
        }
    }
    if (paths.empty()) { return; }
    ops_.request_delete(paths, status_);
    mark_dirty();
}

void FavoritesScreen::handle_key_down(const SDL_KeyboardEvent& key)
{
    using enum InputAction;
    // Phase 50: Shift+I opens import status
    if ((key.key == SDLK_I) && (key.mod & SDL_KMOD_SHIFT)) {
        request(NavKind::ToImportStatus);
        return;
    }
    if (is_quick_switch_key(key)) { quick_switch_.open(); return; }   // switch vault (`)
    if (key.key == SDLK_R) { start_rename(); return; }
    if (handle_detail_panel_scroll(key, detail_.panel)) { return; }
    if (key.key == SDLK_D && (key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) == 0) {
        detail_.panel.open = !detail_.panel.open;
        detail_.key.clear();
        return;
    }
    if (handle_extra_key(key)) { return; }   // subclass consumed it (e.g. tag-view toggle)
    // Phase 68 multiselect: Space toggles (Enter still opens), Ctrl+A
    // selects all / clears, B/X/M act on the selection — grid parity.
    if (key.key == SDLK_SPACE) {
        if (!favs_.empty()) { sel_.toggle(nav_.selected()); }
        return;
    }
    if (key.key == SDLK_A && (key.mod & SDL_KMOD_CTRL)) {
        toggle_select_all();
        return;
    }
    if (key.key == SDLK_B) { toggle_favorite_batch(); return; }
    if (key.key == SDLK_X) { start_export(); return; }
    if (key.key == SDLK_M && !(key.mod & SDL_KMOD_SHIFT)) { start_transfer(); return; }
    if (key.key == SDLK_DELETE) { start_delete_selection(); return; }
    switch (map_key(key.key, key.mod)) {
        case NavLeft:  nav_.move(-1);     follow_scroll_ = true; break;
        case NavRight: nav_.move(1);      follow_scroll_ = true; break;
        case NavUp:    nav_.move(-cols_); follow_scroll_ = true; break;
        case NavDown:  nav_.move(cols_);  follow_scroll_ = true; break;
        case Select:   open_selected();   break;
        case Back:     go_back();          break;
        default:       break;
    }
}

void FavoritesScreen::handle_event(const SDL_Event& e)
{
    if (ops_.handle_event(e)) { return; }
    if (rename_.active()) { (void)rename_.handle_event(vault_, e); return; }
    if (quick_switch_.active()) {
        (void)quick_switch_.handle_event(e);
        if (std::string p; quick_switch_.consume_choice(p))
            request(NavKind::ToUnlock, std::move(p));   // locks current, unlocks chosen
        return;
    }

    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
            handle_key_down(e.key);
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

    // Phase 68: Draw position counter right-aligned at y=40
    if (const std::string pos_label = ui::position_label(nav_.selected(), favs_.size());
        !pos_label.empty()) {
        const auto text_w = static_cast<float>(font_.measure(pos_label));
        const float rx = cW - OX - text_w;
        r.draw_text(font_, rx, 40, pos_label, TEXT_FAINT);
    }

    r.draw_text(font_, OX, 90, "[F1] Help", TEXT_FAINT);

    const auto H = static_cast<float>(win_.height());

    // While a batch worker owns the vault handle, drawing tiles would submit
    // thumbnail decodes against it — draw only the chrome + the modal (Phase 68).
    if (batch_ops_busy()) {
        ops_.render(r, font_, W, H);
        return;
    }

    if (favs_.empty())
        r.draw_text(font_, OX, OY, empty_hint(), TEXT_DIM);

    cols_ = grid_columns(cW - 2 * OX, CELL, GAP);
    const auto [first_idx, last_idx] = grid_visible_range(
        scroll_, CELL, GAP, OY, H, cols_, static_cast<int>(favs_.size()));
    // Clip scrolled tiles to below the fixed header: without this a tile scrolled
    // up past OY paints straight over the title / [F1] Help / status lines.
    const SDL_Rect content_clip{0, static_cast<int>(OY), static_cast<int>(W),
                                static_cast<int>(H - OY)};
    SDL_SetRenderClipRect(r.sdl(), &content_clip);
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

        // Multi-select badge, top-left (Phase 68) — same accent square as the grid.
        if (sel_.contains(i)) {
            const SDL_FRect sbadge{cellr.x + 8, cellr.y + 8, 18, 18};
            r.draw_round_rect(sbadge, RADIUS_SMALL, ACCENT);
            r.draw_round_rect(sbadge, RADIUS_SMALL, BG, /*filled*/ false);
        }

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
    SDL_SetRenderClipRect(r.sdl(), nullptr);

    // Hairline marking where the fixed header ends and the scrolling grid begins.
    r.draw_rect({0.0f, OY - 1.0f, cW, 1.0f}, BORDER);

    if (!status_.empty()) r.draw_text(font_, OX, 118, status_, TEXT_FAINT);

    quick_switch_.render(r, font_, W, H);
    rename_.render(r, font_, W, H);
    ops_.render(r, font_, W, H);

    if (const float pw = detail_panel_width(detail_.panel.open, W);
        pw > 0.0f) {
        const SDL_FRect panel{W - pw, 0.0f, pw, H};
        detail_.content_h = draw_detail_panel(r, font_, panel, detail_.content, detail_.panel.scroll,
                                              vault::vault_settings(vault_).categories);
        detail_.panel.scroll = ui::clamp_scroll(detail_.panel.scroll, detail_.content_h, H);
    }
}

std::vector<ui::HelpGroup> FavoritesScreen::help_groups() const
{
    std::vector<ui::HelpEntry> nav{
        {"Enter", "Open"}, {"Space", "Select"}, {"Ctrl+A", "Select all"},
        {"B", "Favorite (acts on selection)"}, {"X", "Export selection"},
        {"M", "Move/copy selection"}, {"Del", "Delete selection"}, {"R", "Rename"}, {"D", "Toggle the detail panel"},
        {"Shift+I", "Import status"}, {"`", "Switch vault"}, {"Esc", "Back"},
    };
    for (const auto& e : extra_help_entries()) nav.push_back(e);
    return {{"Navigate", nav}};
}

} // namespace ui
