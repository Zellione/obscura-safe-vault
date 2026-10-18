#include "ui/dual_gallery.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <span>

#include "gfx/renderer.h"
#include "gfx/texture_cache.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/folder_dialog.h"
#include "ui/dual_layout.h"
#include "ui/dual_session_state.h"
#include "ui/gallery_grid.h"
#include "ui/gallery_view_setting.h"
#include "ui/help_popup.h"
#include "ui/keybindings.h"
#include "ui/parent_group.h"
#include "ui/progress_modal.h"
#include "ui/text_metrics.h"
#include "ui/vault_unlock_picker.h"
#include "ui/widgets.h"
#include "vault/transfer.h"
#include "vault/vault.h"

namespace ui {

struct DualGalleryScreen::OtherPane {
    vault::Vault vault;
    std::string path;
    std::string label;
    gfx::TextureCache cache;
    GallerySessionState session;
    ImportQueue queue;  // no filesystem import session: this pane is a copy browser

    OtherPane(vault::Vault&& opened, std::string vault_path, std::string name,
              SDL_Renderer* renderer)
        : vault(std::move(opened)), path(std::move(vault_path)), label(std::move(name)),
          cache(renderer)
    {}
};

DualGalleryScreen::DualGalleryScreen(GalleryGrid::GridInitContext ctx,
                                     GalleryGrid::GridDialogs dialogs,
                                     GalleryGrid::GridVaultCtx vault_ctx,
                                     GallerySessionState& session, ImportQueue& queue,
                                     DualSessionState& dual)
    : setup_{ctx, dialogs, vault_ctx, session}, win_(ctx.win), font_(ctx.font), vault_(ctx.vault),
      dual_(dual), queue_(queue)
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

DualGalleryScreen::~DualGalleryScreen()
{
    if (picker_) SDL_StopTextInput(win_.sdl_window());
    job_.abort_and_join();
    if (exclusive_) queue_.set_exclusive(false);
}

vault::Vault& DualGalleryScreen::pane_vault(int pane)
{
    return pane == 1 && other_ ? other_->vault : vault_;
}

void DualGalleryScreen::open_vault_picker()
{
    if (queue_.busy() || setup_.dialogs.file.busy() || setup_.dialogs.folder.busy()) {
        status_ = "Finish the current import or file picker first";
        return;
    }
    picker_ = std::make_unique<VaultUnlockPicker>(setup_.vault_ctx.registry, setup_.dialogs.file,
                                                  win_, setup_.vault_ctx.second_vault);
    picker_->open(setup_.vault_ctx.active_vault_path, false, true);
    SDL_StartTextInput(win_.sdl_window());
}

void DualGalleryScreen::finish_vault_picker()
{
    if (!picker_ || picker_->active()) return;
    SDL_StopTextInput(win_.sdl_window());
    if (!picker_->chosen()) {
        picker_.reset();
        return;
    }
    auto next = std::make_unique<OtherPane>(picker_->take_vault(), picker_->dest_path(),
                                            picker_->dest_label(), win_.sdl_renderer());
    right_->on_exit();
    right_.reset();  // join thumbnail fetches before destroying the old vault
    other_ = std::move(next);
    right_ = std::make_unique<GalleryGrid>(
        GalleryGrid::GridInitContext{win_, font_, other_->vault, other_->cache}, setup_.dialogs,
        GalleryGrid::GridVaultCtx{setup_.vault_ctx.registry, other_->path, nullptr},
        other_->session, other_->queue);
    right_->set_embedded(true);
    apply_split_layout();
    right_->on_enter();
    set_active(1);
    picker_.reset();
    status_ = "Select items, then M to copy to the other pane";
    mark_dirty();
}

void DualGalleryScreen::close_other_vault()
{
    if (!other_) return;
    right_->on_exit();
    right_.reset();
    other_.reset();
    dual_.pane[1] = {};
    right_ = std::make_unique<GalleryGrid>(setup_.ctx, setup_.dialogs, setup_.vault_ctx,
                                           setup_.session, queue_);
    right_->set_embedded(true);
    apply_split_layout();
    right_->on_enter();
    set_active(0);
    status_ = "Second vault locked";
}

// Pane modals own every event, including Tab, F3 and mouse input. Otherwise the
// host could leave a modal worker behind or let the sibling read its live tree.
bool DualGalleryScreen::route_modal(const SDL_Event& e)
{
    GalleryGrid* modal = grid_modal_open(*left_)    ? left_.get()
                         : grid_modal_open(*right_) ? right_.get()
                                                    : nullptr;
    if (!modal) return false;
    const auto split =
        dual_split(static_cast<float>(win_.width()), static_cast<float>(win_.height()));
    forward_pane_event(*modal,
                       translate_event_to_pane(e, modal == left_.get() ? split.left : split.right));
    return true;
}

void DualGalleryScreen::forward_pane_event(GalleryGrid& grid, const SDL_Event& e)
{
    if (other_ && &grid == right_.get() && e.type == SDL_EVENT_KEY_DOWN) {
        // Only browsing and selection in the secondary pane. Mutations/imports
        // and app-wide navigation belong to the active vault's screen.
        switch (e.key.key) {
        case SDLK_UP:
        case SDLK_DOWN:
        case SDLK_LEFT:
        case SDLK_RIGHT:
        case SDLK_HOME:
        case SDLK_END:
        case SDLK_PAGEUP:
        case SDLK_PAGEDOWN:
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_ESCAPE:
        case SDLK_SPACE:
        case SDLK_L:
            break;
        case SDLK_A:
            if (e.key.mod & SDL_KMOD_CTRL) break;
            return;
        case SDLK_D:
            if (is_unmodified(e.key.mod)) break;
            return;
        default:
            status_ = "Use M to copy; open this vault from the manager for other actions";
            mark_dirty();
            return;
        }
    }
    const auto revision = grid_listing_revision(grid);
    grid.handle_event(e);
    if (!other_ && !vault_busy(grid) && grid_listing_revision(grid) != revision)
        (&grid == left_.get() ? *right_ : *left_).on_vault_changed();
    drain_nav(grid);
}

void DualGalleryScreen::update_pane(GalleryGrid& grid, double dt)
{
    const auto revision = grid_listing_revision(grid);
    grid.update(dt);
    if (!other_ && grid_listing_revision(grid) != revision)
        (&grid == left_.get() ? *right_ : *left_).on_vault_changed();
}

void DualGalleryScreen::apply_split_layout()
{
    const auto split = dual_split(static_cast<float>(win_.width()), static_cast<float>(win_.height()));
    // Keep each pane's own status/footer visible above the split-view commands.
    const float footer = line_pitch(font_.pixel_height()) + 12;
    left_->set_layout_override(split.left.w, std::max(0.0f, split.left.h - footer));
    right_->set_layout_override(split.right.w, std::max(0.0f, split.right.h - footer));
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
    if (picker_) SDL_StopTextInput(win_.sdl_window());
    job_.abort_and_join();
    if (exclusive_) {
        queue_.set_exclusive(false);
        exclusive_ = false;
    }
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
    if (!other_) dual_.pane[1] = capture_pane_state(*right_);
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
    right_->set_dialog_pump(!other_ && active_ == 1);
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
        if (gallery_exists(pane_vault(i), path)) {
            continue;  // path still exists, no walk-up needed
        }

        // Path no longer exists; walk up segments until one resolves
        // Start by removing the last segment
        size_t last_slash = path.rfind('/');
        while (last_slash != std::string::npos) {
            path = path.substr(0, last_slash);
            if (gallery_exists(pane_vault(i), path)) {
                // Found a valid ancestor
                jump_pane_to(grid, path);
                break;
            }
            last_slash = path.rfind('/');
        }

        // If we walked all the way up without finding a valid ancestor, jump to root
        // (which is guaranteed to exist)
        if (last_slash == std::string::npos && !gallery_exists(pane_vault(i), path)) {
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

    if (picker_) {
        (void)picker_->handle_event(e);
        finish_vault_picker();
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

    if (route_modal(e)) return;
    if (e.type == SDL_EVENT_KEY_DOWN && handle_split_key(e.key)) return;
    if (route_mouse(e)) return;

    // 8. Everything else: forward to active pane untranslated
    const GalleryView view_before = gallery_view_setting();
    forward_pane_event(active(), e);
    // A pane's L shortcut writes the process-global setting. Mirror it into
    // both live panes before the same event is rendered.
    if (const GalleryView view_after = gallery_view_setting(); view_after != view_before) {
        on_gallery_view_changed(view_after);
    }
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
        if (gallery_exists(pane_vault(active_), path)) {
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
    quiesce_grid(*left_);
    quiesce_grid(*right_);
    queue_.set_exclusive(true);
    exclusive_ = true;
    if (!job_.start_transfer_collection(pane_vault(active_), spec, pane_vault(1 - active_),
                                        dst_path, launch.mode, launch.policy, label)) {
        queue_.set_exclusive(false);
        exclusive_ = false;
        status_ = "Could not start transfer";
    }
}

bool DualGalleryScreen::handle_split_key(const SDL_KeyboardEvent& key)
{
    if ((key.mod & SDL_KMOD_CTRL) && key.key == SDLK_O) {
        open_vault_picker();
        mark_dirty();
        return true;
    }
    if ((key.mod & SDL_KMOD_CTRL) && key.key == SDLK_W) {
        close_other_vault();
        mark_dirty();
        return true;
    }
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
        const GalleryGrid& returning = other_ ? *left_ : active();
        const std::string path = current_gallery_path(returning);
        const int selected = capture_pane_state(returning).selected;
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

    if (vault::vault_is_read_only(pane_vault(1 - active_))) {
        status_ = "Destination vault is read only";
        return;
    }
    if (selected_transfer_paths(active()).empty()) {
        status_ = "Select an item to copy";
        return;
    }

    // Get source and destination galleries
    const std::string& src_path = current_gallery_path(active());
    const std::string& dst_path = current_gallery_path(inactive());

    // Gallery paths within the active pane's selection (for the refusal check)
    std::vector<std::string> gallery_paths_for_check;
    for (const auto& path : selected_transfer_paths(active())) {
        if (gallery_exists(pane_vault(active_), path)) {
            gallery_paths_for_check.push_back(path);
        }
    }

    // Refusal 3: dual_transfer_check (same gallery or cycle)
    if (const auto refusal = dual_transfer_check(src_path, dst_path, gallery_paths_for_check);
        !other_ && refusal != DualTransferRefusal::None) {
        status_ = refusal == DualTransferRefusal::SameGallery
                      ? "Both panes show the same gallery"
                      : "Can't move a gallery into itself";
        return;
    }

    // Refusal 4 (pre-scan): compute colliding galleries
    const std::vector<std::string> conflicts =
        vault::colliding_galleries(pane_vault(1 - active_), dst_path, gallery_paths_for_check);

    // Open the prompt at the destination's leaf name
    const std::string dst_leaf = dst_path.empty() ? "Root" : dst_path.substr(dst_path.rfind('/') + 1);
    const std::string label =
        other_ ? (active_ == 0 ? other_->label : "Active vault") + ": " + dst_leaf : dst_leaf;
    prompt_.open(label, conflicts, other_ != nullptr);
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
    forward_pane_event(grid, translated);
    return true;
}

// Drain a routed pane's pending navigation and re-request it upward.
void DualGalleryScreen::drain_nav(GalleryGrid& grid)
{
    using enum NavKind;
    const Nav n = grid.take_nav();
    if (n.kind == None) return;
    if (other_ && &grid == right_.get()) {
        status_ = "Use the vault manager to open this vault in the full viewer";
        mark_dirty();
        return;
    }
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
    if (picker_) {
        picker_->update();
        finish_vault_picker();
        return;
    }
    // Exclusive ownership ends only after take_outcome has joined the worker.
    if (job_.active()) {
        if (auto outcome = job_.take_outcome()) {
            status_ = outcome->ok ? outcome->status : outcome->error;
            queue_.set_exclusive(false);
            exclusive_ = false;
            clear_grid_selection(active());
            on_vault_changed();  // failures/cancel can also have committed partial work
        }
        mark_dirty();
        return;
    }
    GalleryGrid* busy = vault_busy(*left_)    ? left_.get()
                        : vault_busy(*right_) ? right_.get()
                                              : nullptr;
    if (busy) {
        busy->update(dt);
        if (!vault_busy(*busy)) on_vault_changed();
        mark_dirty();
        return;
    }
    // Drain the input-owning pane's synchronous modal completion first (for
    // example rename), refreshing its sibling before that sibling can update.
    update_pane(active(), dt);
    update_pane(inactive(), dt);
    if (left_->consume_dirty() || right_->consume_dirty()) mark_dirty();
}

void DualGalleryScreen::render(gfx::Renderer& r)
{
    if (job_.active()) {
        draw_op_progress(r, font_, static_cast<float>(win_.width()),
                         static_cast<float>(win_.height()),
                         {.title = "Transferring between panes",
                          .count_line = std::format("{} / {}", job_.done(), job_.total()),
                          .done = job_.done(),
                          .total = job_.total()});
        return;
    }
    if (vault_busy(*left_) || vault_busy(*right_)) {
        GalleryGrid& busy = vault_busy(*left_) ? *left_ : *right_;
        busy.set_layout_override(static_cast<float>(win_.width()),
                                 static_cast<float>(win_.height()));
        busy.render(r);
        apply_split_layout();
        return;
    }
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
        r.draw_text(font_, modal_x + 20.0f, title_y, fit_text(font_, title_text, modal_w - 40),
                    TEXT_DIM);

        // Rows: mode stage or conflict stage
        const float rows_y = modal_y + 60.0f;
        const float row_indent_x = modal_x + 20.0f;
        const float row_width = modal_w - 40.0f;

        if (prompt_.stage() == DualTransferPrompt::Stage::Mode) {
            const std::array<std::string, 3> mode_rows{"Move to " + (dst_label.empty() ? "Root" : dst_label),
                                                       "Copy to " + (dst_label.empty() ? "Root" : dst_label),
                                                       "Cancel"};
            const std::array<std::string, 2> copy_rows{"Copy to " + dst_label, "Cancel"};
            const auto rows = prompt_.copy_only() ? std::span<const std::string>(copy_rows)
                                                  : std::span<const std::string>(mode_rows);
            draw_option_rows(r, font_, row_indent_x, rows_y, row_width, rows, prompt_.selected());
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

    // Vault identities occupy the unused top line in each pane.
    using namespace gfx::theme;
    const std::string left_label = "Active vault: " + setup_.vault_ctx.active_vault_path;
    const std::string right_label = other_ ? "Second vault: " + other_->path : "Active vault";
    r.draw_text(font_, split.left.x + 16, 10, fit_text(font_, left_label, split.left.w - 32),
                TEXT_DIM);
    r.draw_text(font_, split.right.x + 16, 10, fit_text(font_, right_label, split.right.w - 32),
                TEXT_DIM);
    const std::string hint =
        status_.empty()
            ? "[Ctrl+O] Open vault  [Tab] Switch pane  [M] Copy/move  [Ctrl+W] Close vault"
            : status_;
    const float height = line_pitch(font_.pixel_height()) + 12;
    const SDL_FRect banner{0, split.left.h - height, static_cast<float>(win_.width()), height};
    r.draw_rect(banner, BG, true);
    r.draw_text(font_, 16, font_.text_top_for_center(banner.y + height / 2),
                fit_text(font_, hint, banner.w - 32), TEXT);
    if (picker_) {
        const float width = std::min(620.0f, static_cast<float>(win_.width()) - 32);
        const float x = (static_cast<float>(win_.width()) - width) / 2;
        r.draw_rect({0, 0, static_cast<float>(win_.width()), static_cast<float>(win_.height())},
                    MODAL_VEIL);
        picker_->render(r, font_, x, 60, width, "Open vault in right pane:");
        r.draw_text(font_, x, static_cast<float>(win_.height()) - height - 32,
                    fit_text(font_, picker_->error(), width), DANGER);
    }
}

bool DualGalleryScreen::animating() const
{
    return job_.active() || picker_ != nullptr || left_->animating() || right_->animating();
}

bool DualGalleryScreen::blocks_idle_lock() const
{
    return left_->blocks_idle_lock() || right_->blocks_idle_lock() || job_.active();
}

std::vector<HelpGroup> DualGalleryScreen::help_groups() const
{
    if (job_.active()) return {{"Transfer", {{"Esc", "Cancel transfer; keep completed copies"}}}};
    auto groups = active().help_groups();

    // Add split-view-specific help group
    HelpGroup split_group;
    split_group.title = "Split view";
    split_group.entries = {
        {"Ctrl+O", "Open another vault in right pane"},
        {"Ctrl+W", "Close and lock second vault"},
        {"Tab", "Switch pane"},
        {"M", "Move/copy to other pane"},
        {"F3", "Leave split view"},
    };
    groups.push_back(split_group);

    return groups;
}

} // namespace ui
