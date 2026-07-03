#include "ui/search_result_view.h"

#include <algorithm>
#include <format>
#include <span>
#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/texture_cache.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/nav_model.h"
#include "ui/result_grid.h"
#include "ui/tile_thumb.h"
#include "vault/index.h"

namespace ui {

namespace {
constexpr float TOP  = 110.0f;
constexpr float LINE = 30.0f;
}

SearchResultView::SearchResultView(vault::Vault& vault, gfx::Window& win, gfx::FontAtlas& font,
                                   gfx::TextureCache& cache)
    : vault_(vault), win_(win), font_(font), cache_(cache)
{
}

void SearchResultView::update_results(const std::vector<vault::SearchHit>& new_results)
{
    results_ = new_results;
    cur_result_ = std::clamp(cur_result_, 0, std::max(0, static_cast<int>(results_.size()) - 1));
}

void SearchResultView::pump_thumbnails()
{
    while (auto res = grid_worker_.take_result()) {
        if (res->image) { cache_.get_or_upload(res->key, *res->image); }
        else            { grid_failed_.insert(res->key); }
    }
    // Note: mark_dirty() would be called by the parent Screen here.
    // For now, we just update the cache; the parent handles repainting.
}

void SearchResultView::handle_key(const SDL_KeyboardEvent& key)
{
    // Navigation respects the active view: List steps one row per Up/Down (the
    // Phase 18 behaviour); Grid additionally moves Left/Right by one and Up/Down
    // by a whole row (grid_cols_, the last-rendered column count).
    const auto count = static_cast<int>(results_.size());
    auto move = [&](MoveDir d) { cur_result_ = result_move(grid_view_, cur_result_, d, count, grid_cols_); };
    switch (key.key) {
        case SDLK_DOWN:  move(MoveDir::Down);  break;
        case SDLK_UP:    move(MoveDir::Up);    break;
        case SDLK_LEFT:  move(MoveDir::Left);  break;
        case SDLK_RIGHT: move(MoveDir::Right); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: activate_focused(); break;
        default: break;
    }
}

void SearchResultView::activate_focused()
{
    if (cur_result_ < 0 || cur_result_ >= static_cast<int>(results_.size())) return;
    const vault::SearchHit& hit = results_[cur_result_];

    if (hit.is_gallery) {
        if (request_cb_) request_cb_(std::to_underlying(NavKind::ToGallery), hit.path, 0);
        return;
    }

    // Media hit: open the normal viewer over its containing gallery, positioned
    // on this item, so prev/next iterate that gallery.
    const auto segs = split_path(hit.path);
    std::string parent;
    if (segs.size() > 1) parent = join_path(std::span(segs.data(), segs.size() - 1));
    const auto siblings = vault_.list(parent);
    for (int i = 0; i < static_cast<int>(siblings.size()); ++i) {
        if (siblings[i]->name == segs.back()) {
            if (request_cb_) request_cb_(std::to_underlying(NavKind::ToViewer), parent, i);
            return;
        }
    }
}

void SearchResultView::render(gfx::Renderer& r, float x, float colw, bool hot)
{
    using namespace gfx::theme;
    // This render method can be called to draw the entire result grid or list view.
    // For now, we check grid_view_ and render accordingly.
    // Note: render_results() in AdvancedSearchScreen dispatches to us, so we just
    // render the grid view (list view is handled by AdvancedSearchScreen).

    if (grid_view_ != ResultView::Grid) return;

    // Grid view rendering
    if (hot) r.draw_text(font_, x - 16, TOP, ">", ACCENT);
    r.draw_text(font_, x, TOP, std::format("Results ({})", results_.size()), TEXT_DIM);

    constexpr float TILE = 92.0f;
    constexpr float TGAP = 12.0f;
    const float top   = TOP + LINE;
    const auto  H     = static_cast<float>(win_.height());
    const float pitch = TILE + TGAP;
    const int   cols  = grid_columns(colw, TILE, TGAP);   // always >= 1
    grid_cols_ = cols;                              // feed Up/Down stride

    const auto  total   = static_cast<int>(results_.size());
    const int   rows    = (total + cols - 1) / cols;
    const int vis_rows  = std::max(1, static_cast<int>((H - top - 40) / pitch));
    // Scroll so the selected tile's row stays on screen (centred when possible).
    const int cur_row   = (total > 0) ? cur_result_ / cols : 0;
    const int first_row = std::clamp(cur_row - vis_rows / 2, 0, std::max(0, rows - vis_rows));

    const ThumbContext ctx{vault_, cache_, grid_worker_, grid_failed_};
    const GridSpec spec{cols, TILE, TGAP, x, top - static_cast<float>(first_row) * pitch};
    for (int i = first_row * cols; i < total && i < (first_row + vis_rows) * cols; ++i) {
        const SDL_FRect         cell = grid_cell_rect(i, spec);
        const vault::SearchHit& hit  = results_[i];
        if (const bool sel = (i == cur_result_ && hot); sel) {
            r.draw_selection_glow(cell, RADIUS, ACCENT);
            r.draw_round_rect(cell, RADIUS, SURFACE_HI);
            r.draw_round_rect(cell, RADIUS, ACCENT, /*filled*/ false);
        } else {
            r.draw_round_rect(cell, RADIUS, SURFACE);
            r.draw_round_rect(cell, RADIUS, BORDER, /*filled*/ false);
        }
        if (hit.node)
            draw_tile_thumb(r, font_, ctx, *hit.node,
                            {cell.x + 6, cell.y + 6, cell.w - 12, cell.h - 12});
    }
    if (results_.empty()) r.draw_text(font_, x, top, "(no matches)", TEXT_FAINT);
}

} // namespace ui
