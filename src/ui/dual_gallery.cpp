#include "ui/dual_gallery.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <span>

#include "gfx/renderer.h"
#include "gfx/window.h"
#include "ui/dual_layout.h"
#include "ui/dual_session_state.h"
#include "ui/gallery_grid.h"
#include "ui/help_popup.h"
#include "vault/vault.h"

namespace ui {

DualGalleryScreen::DualGalleryScreen(gfx::Window& win, gfx::FontAtlas& font, vault::Vault& vault,
                                     gfx::TextureCache& cache, GalleryGrid::GridDialogs dialogs,
                                     GalleryGrid::GridVaultCtx vault_ctx, GallerySessionState& session,
                                     ImportQueue& queue, DualSessionState& dual)
    : win_(win), font_(font), vault_(vault), dual_(dual)
{
    // Build both grids; don't call on_enter() yet
    left_ = std::make_unique<GalleryGrid>(win, font, vault, cache, dialogs, vault_ctx, session, queue,
                                          GridLocation{dual.pane[0].path, dual.pane[0].selected, dual.pane[0].view});
    right_ = std::make_unique<GalleryGrid>(win, font, vault, cache, dialogs, vault_ctx, session, queue,
                                           GridLocation{dual.pane[1].path, dual.pane[1].selected, dual.pane[1].view});

    // Mark both as embedded (pane mode)
    left_->set_embedded(true);
    right_->set_embedded(true);
}

void DualGalleryScreen::on_enter()
{
    const auto split = dual_split(static_cast<float>(win_.width()), static_cast<float>(win_.height()));

    // Set layout overrides for each pane
    left_->set_layout_override(split.left.w, split.left.h);
    right_->set_layout_override(split.right.w, split.right.h);

    // Enter both grids
    left_->on_enter();
    right_->on_enter();

    // Restore pane state
    restore_pane_state(*left_, dual_.pane[0]);
    restore_pane_state(*right_, dual_.pane[1]);

    // Set active pane
    set_active(dual_.active_pane);
}

void DualGalleryScreen::on_exit()
{
    snapshot();
    left_->on_exit();
    right_->on_exit();
}

void DualGalleryScreen::snapshot()
{
    dual_.pane[0] = capture_pane_state(*left_);
    dual_.pane[1] = capture_pane_state(*right_);
    dual_.active_pane = active_;
    dual_.split_active = false;  // mark that split is no longer active when exiting
}

GalleryGrid& DualGalleryScreen::active()
{
    return active_ == 0 ? *left_ : *right_;
}

const GalleryGrid& DualGalleryScreen::active() const
{
    return active_ == 0 ? *left_ : *right_;
}

GalleryGrid& DualGalleryScreen::inactive()
{
    return active_ == 0 ? *right_ : *left_;
}

void DualGalleryScreen::set_active(int pane)
{
    active_ = pane;
    left_->set_dialog_pump(active_ == 0);
    right_->set_dialog_pump(active_ == 1);
}

void DualGalleryScreen::on_vault_changed()
{
    left_->on_vault_changed();
    right_->on_vault_changed();

    // Walk-up rule: if a pane's current gallery path no longer resolves,
    // walk up path segments until one resolves (root always does).
    // NOTE: The walk-up logic requires checking if a path exists. Since
    // vault::find_gallery is private, the complete implementation will
    // need either: (a) a public vault method to check gallery existence,
    // or (b) moving this logic into gallery_grid.cpp where find_gallery
    // is accessible. For now, both grids' on_vault_changed (which they
    // inherited from Screen) will re-query the vault at their current path,
    // and empty results will be handled by their own refresh logic.
    // TODO: implement full walk-up logic in Task 8 or later phase.
}

void DualGalleryScreen::handle_event(const SDL_Event& e)
{
    // 1. Window resize: recompute the split, set_layout_override both panes
    if (e.type == SDL_EVENT_WINDOW_RESIZED) {
        const auto split = dual_split(static_cast<float>(win_.width()), static_cast<float>(win_.height()));
        left_->set_layout_override(split.left.w, split.left.h);
        right_->set_layout_override(split.right.w, split.right.h);
        mark_dirty();
        return;
    }

    // 2. Transfer prompt (Task 7) would check here - skipping for now

    // 3. Tab (no modifiers): switch panes or forward if overlay active
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_TAB && e.key.mod == 0) {
        // Check if either pane is busy with overlay/naming
        if (!vault_busy(*left_) && !vault_busy(*right_)) {
            set_active(1 - active_);
            mark_dirty();
            return;
        }
        // If busy, fall through to forward to active pane
    }

    // 4. F3: leave split view
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F3) {
        snapshot();
        dual_.split_active = true;
        const std::string path = current_gallery_path(active());
        const int selected = capture_pane_state(active()).selected;
        request(NavKind::ToGallery, path, selected);
        return;
    }

    // 5. M (no mods, active pane not busy): open transfer prompt (Task 7)
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_M && e.key.mod == 0) {
        if (vault_busy(active())) {
            status_ = "Transfer in progress";
            mark_dirty();
            return;
        }
        // Task 7 will open transfer prompt here
        status_ = "Transfer prompt (Task 7)";
        mark_dirty();
        return;
    }

    // 6. Shift+M / Ctrl+D / Shift+C / backtick: disabled in split view
    if (e.type == SDL_EVENT_KEY_DOWN) {
        const bool is_shift_m = e.key.key == SDLK_M && (e.key.mod & SDL_KMOD_SHIFT);
        const bool is_ctrl_d = e.key.key == SDLK_D && (e.key.mod & SDL_KMOD_CTRL);
        const bool is_shift_c = e.key.key == SDLK_C && (e.key.mod & SDL_KMOD_SHIFT);
        const bool is_backtick = e.key.key == SDLK_GRAVE;

        if (is_shift_m || is_ctrl_d || is_shift_c || is_backtick) {
            status_ = "Not available in split view";
            mark_dirty();
            return;
        }
    }

    // 7. Mouse events: route to pane under cursor
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP ||
        e.type == SDL_EVENT_MOUSE_MOTION) {
        const auto split = dual_split(static_cast<float>(win_.width()), static_cast<float>(win_.height()));
        float x = 0.0f;

        // Extract mouse coordinates
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            x = e.button.x;
        } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
            x = e.motion.x;
        }

        int pane_idx = pane_at(split, x);
        const SDL_FRect& pane_rect = pane_idx == 0 ? split.left : split.right;

        // Change active pane if clicking on the other pane
        if (pane_idx != active_) {
            set_active(pane_idx);
            mark_dirty();
        }

        // Translate and forward to the pane
        SDL_Event translated = translate_event_to_pane(e, pane_rect);
        GalleryGrid& grid = (pane_idx == 0 ? *left_ : *right_);
        grid.handle_event(translated);

        // Drain nav from the pane and re-request upward
        Nav n = grid.take_nav();
        if (n.kind != NavKind::None) {
            if (n.kind == NavKind::ToViewer || n.kind == NavKind::ToFavoriteViewer ||
                n.kind == NavKind::ToTagViewer) {
                snapshot();
                dual_.split_active = true;
                dual_.active_pane = active_;
                request(n.kind, n.path, n.index);
            } else if (n.kind != NavKind::ToVaultManager) {
                snapshot();
                request(n.kind, n.path, n.index);
            }
        }
        return;
    }

    // Wheel: forward to pane under cursor WITHOUT changing active pane
    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        const auto split = dual_split(static_cast<float>(win_.width()), static_cast<float>(win_.height()));
        float x = e.wheel.mouse_x;
        int pane_idx = pane_at(split, x);
        const SDL_FRect& pane_rect = pane_idx == 0 ? split.left : split.right;

        SDL_Event translated = translate_event_to_pane(e, pane_rect);
        GalleryGrid& grid = (pane_idx == 0 ? *left_ : *right_);
        grid.handle_event(translated);

        // Drain nav from the pane
        Nav n = grid.take_nav();
        if (n.kind != NavKind::None) {
            if (n.kind == NavKind::ToViewer || n.kind == NavKind::ToFavoriteViewer ||
                n.kind == NavKind::ToTagViewer) {
                snapshot();
                dual_.split_active = true;
                dual_.active_pane = active_;
                request(n.kind, n.path, n.index);
            } else if (n.kind != NavKind::ToVaultManager) {
                snapshot();
                request(n.kind, n.path, n.index);
            }
        }
        return;
    }

    // 8. Everything else: forward to active pane untranslated
    active().handle_event(e);

    // Drain nav from active pane
    Nav n = active().take_nav();
    if (n.kind != NavKind::None) {
        if (n.kind == NavKind::ToViewer || n.kind == NavKind::ToFavoriteViewer ||
            n.kind == NavKind::ToTagViewer) {
            snapshot();
            dual_.split_active = true;
            dual_.active_pane = active_;
            request(n.kind, n.path, n.index);
        } else if (n.kind != NavKind::ToVaultManager) {
            snapshot();
            request(n.kind, n.path, n.index);
        }
    }
}

void DualGalleryScreen::update(double dt)
{
    // Clear old status message each frame to prevent stale displays
    status_.clear();

    // Update both panes
    left_->update(dt);
    right_->update(dt);
    // Poll own transfer job (Task 7) when implemented
}

void DualGalleryScreen::render(gfx::Renderer& r)
{
    const auto split = dual_split(static_cast<float>(win_.width()), static_cast<float>(win_.height()));

    // Render left pane with viewport
    {
        SDL_Rect vp{static_cast<int>(split.left.x), static_cast<int>(split.left.y),
                    static_cast<int>(split.left.w), static_cast<int>(split.left.h)};
        SDL_SetRenderViewport(r.sdl(), &vp);
        left_->render(r);
        SDL_SetRenderViewport(r.sdl(), nullptr);
    }

    // Render right pane with viewport
    {
        SDL_Rect vp{static_cast<int>(split.right.x), static_cast<int>(split.right.y),
                    static_cast<int>(split.right.w), static_cast<int>(split.right.h)};
        SDL_SetRenderViewport(r.sdl(), &vp);
        right_->render(r);
        SDL_SetRenderViewport(r.sdl(), nullptr);
    }

    // NOTE: Divider and accent border rendering will be implemented in Task 9
    // along with full theme integration and status line rendering.
    // For now, both panes are rendered side-by-side with viewports handling separation.
}

bool DualGalleryScreen::animating() const
{
    return left_->animating() || right_->animating();
}

bool DualGalleryScreen::blocks_idle_lock() const
{
    return left_->blocks_idle_lock() || right_->blocks_idle_lock();
}

std::vector<HelpGroup> DualGalleryScreen::help_groups() const
{
    auto groups = active().help_groups();

    // Add split-view-specific help group
    HelpGroup split_group;
    split_group.title = "Split view";
    split_group.entries = {
        {"Tab", "switch pane"},
        {"M", "move/copy to other pane"},
        {"F3", "leave split view"},
    };
    groups.push_back(split_group);

    return groups;
}

} // namespace ui
