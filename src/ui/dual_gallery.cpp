#include "ui/dual_gallery.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <span>

#include "gfx/renderer.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/dual_layout.h"
#include "ui/dual_session_state.h"
#include "ui/gallery_grid.h"
#include "ui/gallery_view_setting.h"
#include "ui/help_popup.h"
#include "ui/keybindings.h"
#include "ui/parent_group.h"
#include "ui/widgets.h"
#include "vault/transfer.h"
#include "vault/vault.h"

namespace ui {

DualGalleryScreen::DualGalleryScreen(GalleryGrid::GridInitContext ctx, GalleryGrid::GridDialogs dialogs,
                                     GalleryGrid::GridVaultCtx vault_ctx, GallerySessionState& session,
                                     ImportQueue& queue, DualSessionState& dual)
    : win_(ctx.win), font_(ctx.font), vault_(ctx.vault), dual_(dual), queue_(queue)
{
    // Build both grids; don't call on_enter() yet
    left_ = std::make_unique<GalleryGrid>(ctx, dialogs, vault_ctx, session, queue,
                                          GridLocation{dual.pane[0].path, dual.pane[0].selected});
    right_ = std::make_unique<GalleryGrid>(ctx, dialogs, vault_ctx, session, queue,
                                           GridLocation{dual.pane[1].path, dual.pane[1].selected});

    // Mark both as embedded (pane mode)
    left_->set_embedded(true);
    right_->set_embedded(true);
}

void DualGalleryScreen::apply_split_layout()
{
    const auto split = dual_split(static_cast<float>(win_.width()), static_cast<float>(win_.height()));
    left_->set_layout_override(split.left.w, split.left.h);
    right_->set_layout_override(split.right.w, split.right.h);
}

void DualGalleryScreen::on_enter()
{
    // Set layout overrides for each pane
    apply_split_layout();

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
    // Phase 78: snapshot() saves config state but does NOT modify split_active.
    // split_active is only set on deliberate F3-leave (before request(ToGallery))
    // or on App::to_dual_gallery() entry. Viewer returns keep split_active true
    // to support restoration to split view.
    left_->on_exit();
    right_->on_exit();
}

void DualGalleryScreen::snapshot()
{
    dual_.pane[0] = capture_pane_state(*left_);
    dual_.pane[1] = capture_pane_state(*right_);
    dual_.active_pane = active_;
    dual_.has_config = true;  // Phase 78: mark that pane configs have been saved
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
    // walk up path segments until one resolves (root always does)
    for (int i = 0; i < 2; ++i) {
        GalleryGrid& grid = (i == 0 ? *left_ : *right_);
        std::string path = current_gallery_path(grid);
        if (path.empty()) {
            continue;  // root always exists
        }

        // Check if the current path still exists in the vault
        if (gallery_exists(vault_, path)) {
            continue;  // path still exists, no walk-up needed
        }

        // Path no longer exists; walk up segments until one resolves
        // Start by removing the last segment
        size_t last_slash = path.rfind('/');
        while (last_slash != std::string::npos) {
            path = path.substr(0, last_slash);
            if (gallery_exists(vault_, path)) {
                // Found a valid ancestor
                jump_pane_to(grid, path);
                break;
            }
            last_slash = path.rfind('/');
        }

        // If we walked all the way up without finding a valid ancestor, jump to root
        // (which is guaranteed to exist)
        if (last_slash == std::string::npos && !gallery_exists(vault_, path)) {
            jump_pane_to(grid, "");  // root
        }
    }
}

void DualGalleryScreen::on_gallery_view_changed(GalleryView view)
{
    left_->on_gallery_view_changed(view);
    right_->on_gallery_view_changed(view);
    mark_dirty();
}

void DualGalleryScreen::handle_event(const SDL_Event& e)
{
    // 1. Window resize: recompute the split, set_layout_override both panes
    if (e.type == SDL_EVENT_WINDOW_RESIZED) {
        apply_split_layout();
        mark_dirty();
        return;
    }

    // 2. Transfer prompt (Task 7): route keys into it; swallow everything else
    // while it is open
    if (prompt_.stage() != DualTransferPrompt::Stage::Closed) {
        if (e.type == SDL_EVENT_KEY_DOWN) handle_prompt_key(e.key);
        return;
    }

    // 2b. Job active (transfer in progress): handle Esc to cancel, block all
    // other events (don't forward to panes)
    if (job_.active()) {
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
            job_.cancel();
            mark_dirty();
        }
        return;
    }

    if (route_busy_pane(e)) return;
    if (e.type == SDL_EVENT_KEY_DOWN && handle_split_key(e.key)) return;
    if (route_mouse(e)) return;

    // 8. Everything else: forward to active pane untranslated
    const GalleryView view_before = gallery_view_setting();
    active().handle_event(e);
    // A pane's L shortcut writes the process-global setting. Mirror it into
    // both live panes before the same event is rendered.
    if (const GalleryView view_after = gallery_view_setting(); view_after != view_before) {
        on_gallery_view_changed(view_after);
    }
    drain_nav(active());
}

void DualGalleryScreen::handle_prompt_key(const SDL_KeyboardEvent& key)
{
    using enum DualTransferPrompt::Key;
    DualTransferPrompt::Key k;
    if (key.key == SDLK_UP) k = Up;
    else if (key.key == SDLK_DOWN) k = Down;
    else if (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER) k = Enter;
    else if (key.key == SDLK_ESCAPE) k = Esc;
    else { mark_dirty(); return; }  // Unhandled key in prompt

    if (const auto launch = prompt_.key(k); launch.fire) fire_transfer(launch);
    mark_dirty();
}

// Fire the transfer job: build the collection spec from the active pane's
// selection and launch it toward the inactive pane's gallery.
void DualGalleryScreen::fire_transfer(const DualTransferPrompt::Launch& launch)
{
    const std::string& dst_path = current_gallery_path(inactive());

    // Split the active pane's selection into media vs gallery paths
    std::vector<std::string> media_paths;
    std::vector<std::string> gallery_paths;
    for (const auto& path : selected_transfer_paths(active())) {
        if (gallery_exists(vault_, path)) {
            gallery_paths.push_back(path);
        } else {
            media_paths.push_back(path);
        }
    }

    // Build CollectionTransferSpec
    CollectionTransferSpec spec{
        group_by_parent(media_paths),
        gallery_paths
    };

    // Build destination label for status message
    const std::string dst_leaf = dst_path.empty() ? "Root" : dst_path.substr(dst_path.rfind('/') + 1);
    const std::string verb = (launch.mode == vault::TransferMode::Copy) ? "Copied" : "Moved";
    const std::string label = verb + " to " + dst_leaf;

    // Launch the job
    job_.start_transfer_collection(vault_, spec, vault_, dst_path,
                                   launch.mode, launch.policy, label);
}

// Input-while-busy routing: if either pane has an active job/import modal,
// route ALL key events to that pane so its progress modal/cancel keys work.
// Returns true only when the key was consumed by a busy, inactive pane; a
// busy ACTIVE pane keeps normal routing.
bool DualGalleryScreen::route_busy_pane(const SDL_Event& e)
{
    if (e.type != SDL_EVENT_KEY_DOWN) return false;
    const bool left_busy = vault_busy(*left_);
    const bool right_busy = vault_busy(*right_);
    if (!left_busy && !right_busy) return false;
    if ((left_busy && active_ != 0) || (right_busy && active_ != 1)) {
        // Busy pane is not active; route key to it anyway
        GalleryGrid& busy_pane = left_busy ? *left_ : *right_;
        busy_pane.handle_event(e);
        if (const Nav n = busy_pane.take_nav(); n.kind != NavKind::None) {
            snapshot();
            request(n.kind, n.path, n.index);
        }
        return true;
    }
    return false;  // busy pane is active; let normal routing handle it
}

bool DualGalleryScreen::handle_split_key(const SDL_KeyboardEvent& key)
{
    // 3. Tab (no modifiers): switch panes — unless a pane is busy with an
    // overlay/naming, in which case Tab forwards to the active pane instead
    if (key.key == SDLK_TAB && is_unmodified(key.mod) && !vault_busy(*left_) && !vault_busy(*right_)) {
        set_active(1 - active_);
        mark_dirty();
        return true;
    }

    // 4. F3: leave split view
    if (key.key == SDLK_F3) {
        snapshot();
        dual_.split_active = false;  // Phase 78: user leaving split view
        const std::string path = current_gallery_path(active());
        const int selected = capture_pane_state(active()).selected;
        request(NavKind::ToGallery, path, selected);
        return true;
    }

    // 5. M (no mods): open transfer prompt (Task 7), with refusal order
    if (key.key == SDLK_M && is_unmodified(key.mod)) {
        open_transfer_prompt();
        mark_dirty();
        return true;
    }

    // 6. Shift+M / Ctrl+D / Shift+C / backtick: disabled in split view
    if ((key.key == SDLK_M && (key.mod & SDL_KMOD_SHIFT)) ||
        (key.key == SDLK_D && (key.mod & SDL_KMOD_CTRL)) ||
        (key.key == SDLK_C && (key.mod & SDL_KMOD_SHIFT)) ||
        key.key == SDLK_GRAVE) {
        status_ = "Not available in split view";
        mark_dirty();
        return true;
    }

    return false;
}

// Phase 78 Task 7: pane-to-pane transfer prompt — run the refusal checks in
// order and open the prompt only when none applies.
void DualGalleryScreen::open_transfer_prompt()
{
    // Refusal 1: queue busy (imports running)
    if (queue_.busy()) {
        status_ = "Imports running — press Shift+I";
        return;
    }

    // Refusal 2: either pane vault_busy or own transfer job active
    if (vault_busy(*left_) || vault_busy(*right_) || job_.active()) {
        status_ = "Transfer in progress";
        return;
    }

    // Get source and destination galleries
    const std::string& src_path = current_gallery_path(active());
    const std::string& dst_path = current_gallery_path(inactive());

    // Gallery paths within the active pane's selection (for the refusal check)
    std::vector<std::string> gallery_paths_for_check;
    for (const auto& path : selected_transfer_paths(active())) {
        if (gallery_exists(vault_, path)) {
            gallery_paths_for_check.push_back(path);
        }
    }

    // Refusal 3: dual_transfer_check (same gallery or cycle)
    if (const auto refusal = dual_transfer_check(src_path, dst_path, gallery_paths_for_check);
        refusal != DualTransferRefusal::None) {
        status_ = refusal == DualTransferRefusal::SameGallery
                      ? "Both panes show the same gallery"
                      : "Can't move a gallery into itself";
        return;
    }

    // Refusal 4 (pre-scan): compute colliding galleries
    const std::vector<std::string> conflicts = vault::colliding_galleries(
        vault_, dst_path, gallery_paths_for_check);

    // Open the prompt at the destination's leaf name
    const std::string dst_leaf = dst_path.empty() ? "Root" : dst_path.substr(dst_path.rfind('/') + 1);
    prompt_.open(dst_leaf, conflicts);
}

// 7. Mouse buttons/motion: route to the pane under the cursor (activating it);
// wheel: route to the pane under the cursor WITHOUT changing the active pane.
bool DualGalleryScreen::route_mouse(const SDL_Event& e)
{
    const bool is_button = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP;
    const bool is_motion = e.type == SDL_EVENT_MOUSE_MOTION;
    const bool is_wheel = e.type == SDL_EVENT_MOUSE_WHEEL;
    if (!is_button && !is_motion && !is_wheel) return false;

    // Extract mouse coordinates
    float x = 0.0f;
    if (is_button) x = e.button.x;
    else if (is_motion) x = e.motion.x;
    else x = e.wheel.mouse_x;

    const auto split = dual_split(static_cast<float>(win_.width()), static_cast<float>(win_.height()));
    const int pane_idx = pane_at(split, x);
    const SDL_FRect& pane_rect = pane_idx == 0 ? split.left : split.right;

    // Change active pane if clicking on the other pane (never for wheel)
    if (!is_wheel && pane_idx != active_) {
        set_active(pane_idx);
        mark_dirty();
    }

    // Translate and forward to the pane, then drain its nav
    const SDL_Event translated = translate_event_to_pane(e, pane_rect);
    GalleryGrid& grid = pane_idx == 0 ? *left_ : *right_;
    grid.handle_event(translated);
    drain_nav(grid);
    return true;
}

// Drain a routed pane's pending navigation and re-request it upward.
void DualGalleryScreen::drain_nav(GalleryGrid& grid)
{
    using enum NavKind;
    const Nav n = grid.take_nav();
    if (n.kind == None) return;
    if (n.kind == ToViewer || n.kind == ToFavoriteViewer || n.kind == ToTagViewer) {
        snapshot();
        // Phase 78: split_active stays true (we're in split view); only set
        // false on deliberate F3-leave. Viewer will return to split via
        // apply_nav's from_viewer && split_active check.
        dual_.active_pane = active_;
        request(n.kind, n.path, n.index);
    } else if (n.kind != ToVaultManager) {
        snapshot();
        request(n.kind, n.path, n.index);
    }
}

void DualGalleryScreen::update(double dt)
{
    // Clear old status message each frame to prevent stale displays
    // (unless a job just finished, which sets a new message)
    std::string old_status = status_;
    status_.clear();

    // Update both panes
    left_->update(dt);
    right_->update(dt);

    // Poll the transfer job (Task 7)
    if (auto outcome = job_.take_outcome()) {
        if (outcome->ok) {
            // Success: show completion status
            status_ = outcome->status;
            // Clear active pane selection to avoid stale selection after transfer
            clear_grid_selection(active());
            // Refresh both panes including walk-up
            on_vault_changed();
        } else {
            // Failure: show error
            status_ = outcome->error;
        }
        mark_dirty();
    } else if (job_.active()) {
        // Job still running: show progress (done/total)
        const int done = job_.done();
        const int total = job_.total();
        status_ = std::format("Moving… {}/{}", done, total);
        mark_dirty();
    }
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

    // Reset viewport for global (non-pane) drawing
    SDL_SetRenderViewport(r.sdl(), nullptr);

    // Draw divider strip (full height, theme BORDER color)
    using namespace gfx::theme;
    r.draw_rect(split.divider, BORDER, /*filled*/ true);

    // Draw 2px accent border around the active pane
    {
        const SDL_FRect& active_pane_rect = active_ == 0 ? split.left : split.right;
        constexpr float border_width = 2.0f;

        // Top border
        SDL_FRect top{active_pane_rect.x, active_pane_rect.y, active_pane_rect.w, border_width};
        r.draw_rect(top, ACCENT, /*filled*/ true);

        // Bottom border
        SDL_FRect bottom{active_pane_rect.x, active_pane_rect.y + active_pane_rect.h - border_width,
                         active_pane_rect.w, border_width};
        r.draw_rect(bottom, ACCENT, /*filled*/ true);

        // Left border
        SDL_FRect left{active_pane_rect.x, active_pane_rect.y, border_width, active_pane_rect.h};
        r.draw_rect(left, ACCENT, /*filled*/ true);

        // Right border
        SDL_FRect right{active_pane_rect.x + active_pane_rect.w - border_width, active_pane_rect.y,
                        border_width, active_pane_rect.h};
        r.draw_rect(right, ACCENT, /*filled*/ true);
    }

    // Draw the transfer prompt modal if open (centered over entire window)
    if (prompt_.stage() != DualTransferPrompt::Stage::Closed) {
        using namespace gfx::theme;
        const auto W = static_cast<float>(win_.width());
        const auto H = static_cast<float>(win_.height());

        // Modal background: semi-transparent overlay
        r.draw_rect(SDL_FRect{0, 0, W, H}, MODAL_VEIL_TRANSLUCENT, /*filled*/ true);

        // Modal panel: 400px wide, centered
        static constexpr float modal_w = 400.0f;
        static constexpr float modal_h = 280.0f;
        const float modal_x = (W - modal_w) * 0.5f;
        const float modal_y = (H - modal_h) * 0.5f;
        SDL_FRect modal_bg{modal_x, modal_y, modal_w, modal_h};
        r.draw_rect(modal_bg, SURFACE, /*filled*/ true);
        r.draw_rect(modal_bg, BORDER, /*filled*/ false);

        // Title: show destination gallery name
        const std::string& dst_label = prompt_.dst_label();
        const std::string title_text = "Transfer to " + (dst_label.empty() ? "Root" : dst_label);
        const float title_y = modal_y + 16.0f;
        r.draw_text(font_, modal_x + 20.0f, title_y, title_text, TEXT_DIM);

        // Rows: mode stage or conflict stage
        const float rows_y = modal_y + 60.0f;
        const float row_indent_x = modal_x + 20.0f;
        const float row_width = modal_w - 40.0f;

        if (prompt_.stage() == DualTransferPrompt::Stage::Mode) {
            const std::array<std::string, 3> mode_rows{"Move to " + (dst_label.empty() ? "Root" : dst_label),
                                                       "Copy to " + (dst_label.empty() ? "Root" : dst_label),
                                                       "Cancel"};
            draw_option_rows(r, font_, row_indent_x, rows_y, row_width,
                           std::span<const std::string>(mode_rows), prompt_.selected());
        } else if (prompt_.stage() == DualTransferPrompt::Stage::Conflict) {
            const std::array<std::string, 3> conflict_rows{"Combine into existing", "Rename (_2)", "Cancel"};
            draw_option_rows(r, font_, row_indent_x, rows_y, row_width,
                           std::span<const std::string>(conflict_rows), prompt_.selected());
        }

        // Help text at bottom
        const float help_y = modal_y + modal_h - 30.0f;
        r.draw_text(font_, row_indent_x, help_y,
                   "[Up/Down] choose  [Enter] select  [Esc] cancel", TEXT_FAINT);
    }

    // Draw status line if not empty (small banner at divider bottom)
    if (!status_.empty()) {
        // Status line is rendered at the bottom of the divider as a small notification
        // Position: divider's bottom-left, full divider width, small height
        static constexpr float status_height = 20.0f;
        SDL_FRect status_bg{split.divider.x, split.divider.y + split.divider.h - status_height,
                            split.divider.w, status_height};
        // Draw background to ensure text is readable
        r.draw_rect(status_bg, gfx::theme::BG, /*filled*/ true);

        // Render the status text centered vertically in the banner, left-aligned
        const float ty = font_.text_top_for_center(status_bg.y + status_bg.h * 0.5f);
        static constexpr float text_x_offset = 4.0f;  // small padding from left edge
        r.draw_text(font_, status_bg.x + text_x_offset, ty, status_, gfx::theme::TEXT);
    }
}

bool DualGalleryScreen::animating() const
{
    return left_->animating() || right_->animating();
}

bool DualGalleryScreen::blocks_idle_lock() const
{
    return left_->blocks_idle_lock() || right_->blocks_idle_lock() || job_.active();
}

std::vector<HelpGroup> DualGalleryScreen::help_groups() const
{
    auto groups = active().help_groups();

    // Add split-view-specific help group
    HelpGroup split_group;
    split_group.title = "Split view";
    split_group.entries = {
        {"Tab", "Switch pane"},
        {"M", "Move/copy to other pane"},
        {"F3", "Leave split view"},
    };
    groups.push_back(split_group);

    return groups;
}

} // namespace ui
