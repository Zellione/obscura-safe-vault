#include "ui/transfer_dialog.h"

#include <algorithm>
#include <format>
#include <utility>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "platform/file_dialog.h"
#include "platform/paths.h"
#include "platform/path_utf8.h"
#include "platform/vault_registry.h"
#include "ui/keybindings.h"
#include "ui/progress_modal.h"
#include "ui/text_input_event.h"
#include "ui/text_metrics.h"
#include "ui/widgets.h"
#include "vault/transfer.h"

namespace ui {

namespace {
constexpr const char* kNewGalleryRow = "+ New gallery…";

void render_direction_body(gfx::Renderer& r, gfx::FontAtlas& font,
                           float ix, float iy, float mw, int direction_sel)
{
    using namespace gfx::theme;
    r.draw_text(font, ix, iy + 36, "Direction:", TEXT_DIM);
    const std::vector<std::string> directions = {"To another vault…", "From another vault…"};
    for (size_t i = 0; i < directions.size(); ++i) {
        const float ry = iy + 72 + static_cast<float>(i) * 34.0f;
        const bool  on = (static_cast<int>(i) == direction_sel);
        if (on) r.draw_round_rect({ix, ry, mw - 40, 30}, RADIUS_SMALL, SURFACE_HI);
        r.draw_text(font, ix + 8, ry + 4, fit_text(font, directions[i], mw - 56), on ? TEXT : TEXT_DIM);
    }
    r.draw_text(font, ix, iy + 150,
                fit_text(font, "[Up/Down] choose  [Enter] next", mw - 40), TEXT_FAINT);
}

void render_mode_body(gfx::Renderer& r, gfx::FontAtlas& font, float ix, float iy, float mw,
                      vault::TransferMode mode)
{
    using namespace gfx::theme;
    r.draw_text(font, ix, iy + 36, "Action:", TEXT_DIM);
    const std::vector<std::string> modes = {"Move", "Copy"};
    const int msel = (mode == vault::TransferMode::Copy) ? 1 : 0;
    for (size_t i = 0; i < modes.size(); ++i) {
        const float ry = iy + 72 + static_cast<float>(i) * 34.0f;
        const bool  on = (static_cast<int>(i) == msel);
        if (on) r.draw_round_rect({ix, ry, mw - 40, 30}, RADIUS_SMALL, SURFACE_HI);
        r.draw_text(font, ix + 8, ry + 4, fit_text(font, modes[i], mw - 56), on ? TEXT : TEXT_DIM);
    }
    r.draw_text(font, ix, iy + 150,
                fit_text(font, "[Up/Down] choose  [Enter] next", mw - 40), TEXT_FAINT);
}
} // namespace

TransferDialog::TransferDialog(vault::Vault& src, std::string src_path,
                               platform::VaultRegistry& registry,
                               platform::FileDialog& dlg, gfx::Window& win, SecondVaultSession* second)
    : src_(src), src_path_(std::move(src_path)), win_(win), picker_dest_(registry, dlg, win, second) {}

void TransferDialog::set_current_gallery(std::string path)
{
    pull_.has_current = true;
    pull_.current_gallery = std::move(path);
}

void TransferDialog::open(std::string src_gallery, std::vector<std::string> filenames)
{
    active_       = true;
    source_       = Source::Images;
    stage_        = pull_.has_current ? Stage::Direction : Stage::Mode;
    mode_         = vault::TransferMode::Move;
    src_gallery_  = std::move(src_gallery);
    filenames_    = std::move(filenames);
    error_.clear();
    naming_ = false;
    name_buf_.clear();
    conflict_ = {};
    pull_.active = false;
    pull_.direction_sel = 0;

    // The new-gallery name overlay consumes SDL_EVENT_TEXT_INPUT, which SDL3 only
    // delivers while text input is active.
    SDL_StartTextInput(win_.sdl_window());
}

void TransferDialog::open_gallery(std::string src_gallery)
{
    open("", {});                  // reuse open() to reset all state + direction state
    source_      = Source::Gallery;
    src_gallery_ = std::move(src_gallery);
}

void TransferDialog::open_galleries(std::vector<std::string> src_paths)
{
    open("", {});                  // reuse open() to reset all state + build candidates
    source_        = Source::Galleries;
    src_galleries_ = std::move(src_paths);
}

void TransferDialog::open_mixed(std::string src_gallery, std::vector<std::string> media_names,
                                std::vector<std::string> gallery_paths)
{
    // A grid mixed selection is one Collection with a single media group.
    std::vector<ParentGroup> groups;
    if (!media_names.empty()) {
        groups.push_back({.parent = std::move(src_gallery), .names = std::move(media_names)});
    }
    open_collection(std::move(groups), std::move(gallery_paths));
}

void TransferDialog::open_collection(std::vector<ParentGroup> media_groups,
                                     std::vector<std::string> gallery_paths)
{
    open("", {});                  // reuse open() to reset all state + build candidates
    source_        = Source::Collection;
    media_groups_  = std::move(media_groups);
    src_galleries_ = std::move(gallery_paths);
}

void TransferDialog::close()
{
    picker_dest_.close();   // wipes/locks the transient destination vault, if any
    SDL_StopTextInput(win_.sdl_window());
    active_ = false;
}

// The vault the transfer writes into: the active vault for a same-vault transfer,
// otherwise the transiently-unlocked destination. Never lock src_ here — App owns it.
vault::Vault& TransferDialog::dest_vault() noexcept
{
    return picker_dest_.is_self() ? src_ : picker_dest_.unlocked_vault();
}

// --- stage transitions ----------------------------------------------------

void TransferDialog::rebuild_targets()
{
    const vault::Vault& dv = dest_vault();
    // Mixed uses the galleries constraint: any gallery holds media (Phase 46),
    // but a subtree target must exclude move-into-own-subtree cycles.
    std::vector<std::string> targets = (source_ == Source::Images)
        ? vault::image_target_galleries(dv)
        : vault::gallery_target_parents(dv);
    picker_.set_items(std::move(targets));
    picker_.set_pinned_suffix(kNewGalleryRow);
}

void TransferDialog::choose_gallery()
{
    const auto& shown = picker_.filtered();
    const int sel = picker_.selected();
    if (sel < 0 || sel >= static_cast<int>(shown.size())) return;
    const std::string& picked = shown[static_cast<size_t>(sel)];
    if (picked == kNewGalleryRow) { naming_ = true; name_buf_.clear(); return; }
    do_move(picked);
}

std::vector<std::string> TransferDialog::galleries_for_conflict_scan() const
{
    using enum Source;
    if (source_ == Gallery)  return {src_gallery_};
    if (source_ == Galleries || source_ == Collection) return src_galleries_;
    return {};   // Images: files always skip; nothing to ask
}

void TransferDialog::do_move(std::string_view dst_target)
{
    // Main-thread pre-scan (the index tree is main-thread-only and the job has
    // not launched yet, so this is race-free): if any transferred gallery
    // already exists at the destination, ask ONCE how to resolve — the choice
    // applies to every colliding gallery in this run.
    if (const auto scan = galleries_for_conflict_scan(); !scan.empty()) {
        const auto clashes = vault::colliding_galleries(dest_vault(), dst_target, scan);
        if (!clashes.empty()) {
            conflict_.target = std::string(dst_target);
            conflict_.count = static_cast<int>(clashes.size());
            conflict_.sel = 0;
            stage_ = Stage::Conflict;
            return;
        }
    }
    launch_current(dst_target, vault::CollisionPolicy::Fail);
}

void TransferDialog::launch_current(std::string_view target, vault::CollisionPolicy policy)
{
    if (pull_.active) {
        // Pull: source galleries from picker_dest_.unlocked_vault() into src_ (active vault)
        // at target destination.
        vault::Vault& src_vault = picker_dest_.unlocked_vault();   // source in pull mode
        const std::string where = picker_dest_.dest_label();       // source vault label

        run_.job.start_transfer_galleries(src_vault, src_galleries_, src_, std::string(target),
                                          mode_, policy, where);
    } else {
        // Push: existing behavior (into dest_vault_)
        launch_transfer(target, policy);
        return;   // launch_transfer sets stage_ = Running
    }
    stage_ = Stage::Running;
}

void TransferDialog::launch_transfer(std::string_view dst_target, vault::CollisionPolicy policy)
{
    vault::Vault& dv = dest_vault();
    const std::string where = picker_dest_.dest_label();

    // Launch the move/copy on a worker thread so a large gallery never freezes the
    // UI (Phase 25). Do NOT close() yet — the transiently-unlocked destination
    // (dest_.vault) must stay alive for the job's life; the dialog stays active in
    // the Running stage showing a progress modal, and closes when update() drains
    // the outcome. The host grid stops touching the vault while job_active().
    if (source_ == Source::Gallery)
        run_.job.start_transfer_gallery(src_, src_gallery_, dv, std::string(dst_target),
                                        mode_, policy, where);
    else if (source_ == Source::Galleries)
        run_.job.start_transfer_galleries(src_, src_galleries_, dv, std::string(dst_target),
                                          mode_, policy, where);
    else if (source_ == Source::Collection)
        run_.job.start_transfer_collection(src_, {media_groups_, src_galleries_}, dv,
                                           std::string(dst_target), mode_, policy, where);
    else
        run_.job.start_transfer_images(src_, src_gallery_, filenames_, dv, std::string(dst_target),
                                       mode_, where);
    stage_ = Stage::Running;
}

// --- per-stage key handlers ------------------------------------------------

bool TransferDialog::handle_direction_key(SDL_Keycode k)
{
    if (k == SDLK_UP || k == SDLK_DOWN || k == SDLK_LEFT || k == SDLK_RIGHT)
        pull_.direction_sel = (pull_.direction_sel == 0) ? 1 : 0;
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
        pull_.active = (pull_.direction_sel == 1);
        stage_ = Stage::Mode;
    }
    return true;
}

bool TransferDialog::handle_mode_key(SDL_Keycode k)
{
    using enum vault::TransferMode;
    if (k == SDLK_UP || k == SDLK_DOWN || k == SDLK_LEFT || k == SDLK_RIGHT)
        mode_ = (mode_ == Move) ? Copy : Move;
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
        if (pull_.active) {
            picker_dest_.open(src_path_, /*include_self=*/false);
        } else {
            picker_dest_.open(src_path_);
        }
        stage_ = Stage::PickingDest;
    }
    return true;
}

bool TransferDialog::handle_conflict_key(SDL_Keycode k)
{
    if (k == SDLK_UP)   conflict_.sel = (conflict_.sel + 2) % 3;
    if (k == SDLK_DOWN) conflict_.sel = (conflict_.sel + 1) % 3;
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
        if (conflict_.sel == 0) {
            launch_current(conflict_.target, vault::CollisionPolicy::Combine);
        } else if (conflict_.sel == 1) {
            launch_current(conflict_.target, vault::CollisionPolicy::Suffix);
        } else {
            close();
        }
    }
    return true;
}

bool TransferDialog::handle_gallery_key(SDL_Keycode k)
{
    if (k == SDLK_UP)   picker_.move(-1);
    if (k == SDLK_DOWN) picker_.move(1);
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) choose_gallery();
    return true;
}

bool TransferDialog::handle_src_galleries_key(SDL_Keycode k)
{
    if (k == SDLK_SPACE) {
        picker_.toggle_checked();
        return true;
    }
    if (k == SDLK_UP)   picker_.move(-1);
    if (k == SDLK_DOWN) picker_.move(1);
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
        auto paths = ui::drop_descendant_paths(picker_.checked());
        if (paths.empty()) return true;   // ignore if nothing checked
        // Pre-scan for collisions: the destination is src_ (active vault), parent is pull_.current_gallery
        if (const auto clashes = vault::colliding_galleries(src_, pull_.current_gallery, paths);
            !clashes.empty()) {
            conflict_.target = pull_.current_gallery;
            conflict_.count = static_cast<int>(clashes.size());
            conflict_.sel = 0;
            src_galleries_ = std::move(paths);
            stage_ = Stage::Conflict;
            return true;
        }
        // No conflicts, launch the pull transfer
        src_galleries_ = std::move(paths);
        launch_current(pull_.current_gallery, vault::CollisionPolicy::Fail);
    }
    return true;
}

bool TransferDialog::handle_naming_event(const SDL_Event& e)
{
    // Precedence rule (Phase 54): the name field consumes editing keys first.
    if (handle_text_input_event(name_buf_, e)) return true;
    if (e.type != SDL_EVENT_KEY_DOWN) return true;
    switch (e.key.key) {
        case SDLK_ESCAPE:
            naming_ = false;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (!name_buf_.empty() &&
                dest_vault().create_gallery(name_buf_.str()) == vault::VaultResult::Ok) {
                rebuild_targets();
                naming_ = false;
                do_move(name_buf_.str());   // move straight into the new gallery
            } else {
                error_ = "Could not create that gallery.";
                naming_ = false;
            }
            break;
        default:
            break;
    }
    return true;
}

// --- input ----------------------------------------------------------------

// PickingDest stage: delegate entirely to picker_dest_ (includes Esc handling).
bool TransferDialog::handle_picking_dest_event(const SDL_Event& e)
{
    const bool consumed = picker_dest_.handle_event(e);
    if (!picker_dest_.active()) {
        if (picker_dest_.chosen()) {
            if (pull_.active) {
                // For pull, load galleries from the unlocked source vault (picker_dest_'s vault)
                picker_.set_items(vault::all_galleries(picker_dest_.unlocked_vault()));
                picker_.set_multi(true);
                picker_.set_pinned_suffix("");   // no "New gallery" when pulling
                stage_ = Stage::PickSrcGalleries;
            } else {
                // For push, load target galleries from the destination
                rebuild_targets();
                stage_ = Stage::PickGallery;
            }
        } else {
            close();   // Esc inside the picker cancelled the whole dialog
        }
    }
    return consumed;
}

// PickGallery filter typing (Phase 44 Part 1) — '/' opens it; Esc clears the
// filter before the outer Esc-closes-the-dialog handling ever sees it.
bool TransferDialog::handle_gallery_filter_event(const SDL_Event& e)
{
    // Precedence rule (Phase 54): the filter field consumes editing keys first.
    if (const uint64_t rev = picker_.filter_input().revision();
        handle_text_input_event(picker_.filter_input(), e)) {
        if (picker_.filter_input().revision() != rev) picker_.refilter();
        return true;
    }
    if (e.type != SDL_EVENT_KEY_DOWN) return true;
    switch (e.key.key) {
        case SDLK_ESCAPE:
            if (!picker_.filter().empty()) { picker_.filter_clear(); return true; }
            picker_.close_filter();
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: choose_gallery(); return true;
        case SDLK_UP:       picker_.move(-1);  return true;
        case SDLK_DOWN:     picker_.move(1);   return true;
        default: return true;   // swallow other keys while typing a filter
    }
}

// PickSrcGalleries filter typing — same as PickGallery, but different key handler afterward
bool TransferDialog::handle_src_galleries_filter_event(const SDL_Event& e)
{
    // Precedence rule (Phase 54): the filter field consumes editing keys first.
    if (const uint64_t rev = picker_.filter_input().revision();
        handle_text_input_event(picker_.filter_input(), e)) {
        if (picker_.filter_input().revision() != rev) picker_.refilter();
        return true;
    }
    if (e.type != SDL_EVENT_KEY_DOWN) return true;
    switch (e.key.key) {
        case SDLK_ESCAPE:
            if (!picker_.filter().empty()) { picker_.filter_clear(); return true; }
            picker_.close_filter();
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return handle_src_galleries_key(e.key.key);
        case SDLK_UP:       picker_.move(-1);  return true;
        case SDLK_DOWN:     picker_.move(1);   return true;
        case SDLK_SPACE:    picker_.toggle_checked(); return true;
        default: return true;   // swallow other keys while typing a filter
    }
}

bool TransferDialog::handle_stage_key(const SDL_KeyboardEvent& key)
{
    using enum Stage;
    switch (stage_) {
        case Direction:   return handle_direction_key(key.key);
        case Mode:        return handle_mode_key(key.key);
        case Conflict:    return handle_conflict_key(key.key);
        case PickGallery:
            if (is_search_key(key)) { picker_.open_filter(); return true; }
            return handle_gallery_key(key.key);
        case PickSrcGalleries:
            if (is_search_key(key)) { picker_.open_filter(); return true; }
            return handle_src_galleries_key(key.key);
        case PickingDest: return true;   // should not reach here (handled above)
        case Running:     return true;   // handled above (Esc→cancel); nothing else
    }
    return true;
}

bool TransferDialog::handle_event(const SDL_Event& e)
{
    if (!active_) return false;

    // While the transfer worker runs, swallow all input except Esc → cooperative
    // cancel (files committed so far remain; see transfer_gallery/transfer_images).
    if (stage_ == Stage::Running) {
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) run_.job.cancel();
        return true;
    }

    if (stage_ == Stage::PickingDest) return handle_picking_dest_event(e);

    // New-gallery name entry (overlays the PickGallery stage).
    if (naming_) return handle_naming_event(e);

    if (stage_ == Stage::PickGallery && picker_.filter_open()) return handle_gallery_filter_event(e);
    if (stage_ == Stage::PickSrcGalleries && picker_.filter_open()) return handle_src_galleries_filter_event(e);

    if (e.type != SDL_EVENT_KEY_DOWN) return true;   // modal swallows other events
    if (e.key.key == SDLK_ESCAPE) { close(); return true; }

    return handle_stage_key(e.key);
}

void TransferDialog::update()
{
    // Drain the background transfer once the worker finishes: record the status,
    // then close() (which wipes the destination key now that it is done with it).
    if (stage_ == Stage::Running) {
        if (auto oc = run_.job.take_outcome()) {
            run_.completion.status = oc->ok ? std::move(oc->status) : oc->error;
            run_.completion.failed_total = oc->failed;
            run_.completion.failures = std::move(oc->failures);
            run_.done   = true;
            // Phase 66: a Keep mode hands the unlocked destination to the warm
            // slot (or slides its window) BEFORE close() wipes anything.
            picker_dest_.release_to_slot();
            close();
        }
        return;
    }

    if (stage_ == Stage::PickingDest) picker_dest_.update();
}

bool TransferDialog::consume_completed(TransferCompletion& out)
{
    if (!run_.done) return false;
    out = std::move(run_.completion);
    // For pull transfers, change "to source" wording to "from source"
    if (pull_.active && !out.status.empty()) {
        // Replace " to " with " from " (the label is the source vault name in pull mode)
        size_t pos = out.status.rfind(" to ");
        if (pos != std::string::npos) {
            out.status.replace(pos, 4, " from ");
        }
    }
    run_.done = false;
    return true;
}

// --- render ---------------------------------------------------------------

void TransferDialog::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H)
{
    if (!active_) return;

    // While the worker runs, show a veiling progress modal instead of the dialog —
    // the host grid has already suppressed its own drawing (job_active()).
    if (stage_ == Stage::Running) {
        const char* verb = (mode_ == vault::TransferMode::Copy) ? "Copying…" : "Moving…";
        const int total = run_.job.total();
        const int done  = run_.job.done();
        const std::string count =
            total > 0 ? std::format("{} / {} files", done, total) : "Preparing…";
        draw_op_progress(r, font, W, H,
                         {.title = verb, .count_line = count, .done = done, .total = total});
        return;
    }

    using namespace gfx::theme;

    const float mw = W * 0.6f;
    const float mh = H * 0.6f;
    const float mx = (W - mw) / 2;
    const float my = (H - mh) / 2;
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, SURFACE);
    r.draw_round_rect({mx, my, mw, mh}, RADIUS, ACCENT, /*filled*/ false);

    const float ix = mx + 20;
    const float iy = my + 20;
    std::string title;
    if (pull_.active) {
        title = "Pull from another vault";
    } else {
        const char* verb = (mode_ == vault::TransferMode::Copy) ? "Copy" : "Move";
        if (source_ == Source::Gallery) {
            title = std::format("{} gallery \"{}\"", verb,
                                 platform::path_to_utf8(platform::utf8_to_path(src_gallery_).filename()));
        } else if (source_ == Source::Galleries) {
            title = std::format("{} {} galleries", verb, src_galleries_.size());
        } else {
            title = std::format("{} {} image(s)", verb, filenames_.size());
        }
    }
    r.draw_text(font, ix, iy, title, TEXT);

    render_body(r, font, ix, iy, mw, mh, my);

    const std::string& err = !error_.empty() ? error_ : picker_dest_.error();
    if (!err.empty()) r.draw_text(font, ix, my + mh - 30, err, DANGER);
}


void TransferDialog::render_conflict_body(gfx::Renderer& r, gfx::FontAtlas& font,
                                          float ix, float iy, float mw) const
{
    using namespace gfx::theme;
    const std::string title = std::format("{} {} already exist{} at destination",
                                          conflict_.count,
                                          conflict_.count == 1 ? "gallery" : "galleries",
                                          conflict_.count == 1 ? "s" : "");
    r.draw_text(font, ix, iy + 36, fit_text(font, title, mw - 40), TEXT_DIM);
    const std::vector<std::string> options = {
        "Combine into existing (files skip, sub-galleries merge)",
        "Rename with _2 suffix",
        "Cancel"
    };
    const float row_h = ui::line_pitch(font.pixel_height());
    for (size_t i = 0; i < options.size(); ++i) {
        const float ry = iy + 72 + static_cast<float>(i) * row_h;
        const bool  on = (static_cast<int>(i) == conflict_.sel);
        if (on) r.draw_round_rect({ix, ry, mw - 40, 30}, RADIUS_SMALL, SURFACE_HI);
        r.draw_text(font, ix + 8, ry + 4, fit_text(font, options[i], mw - 56), on ? TEXT : TEXT_DIM);
    }
    r.draw_text(font, ix, iy + 180,
                fit_text(font, "[Up/Down] Select   [Enter] Confirm   [Esc] Close", mw - 40), TEXT_FAINT);
}

void TransferDialog::render_pick_gallery_body(gfx::Renderer& r, gfx::FontAtlas& font,
                                              float ix, float iy, float mw, float mh, float my)
{
    using namespace gfx::theme;
    r.draw_text(font, ix, iy + 36,
                source_ == Source::Gallery ? "Destination parent gallery:"
                                           : "Destination gallery:",
                TEXT_DIM);
    const float list_top = iy + 72;
    const float row_h    = 34.0f;
    const float list_bottom = naming_ ? my + mh - 100 : my + mh - 20;
    const int   visible_rows = std::max(1, static_cast<int>((list_bottom - list_top) / row_h));
    const auto  g = picker_.geom(visible_rows);
    const auto& shown = picker_.filtered();
    for (int i = g.first; i < g.first + g.visible && i < static_cast<int>(shown.size()); ++i) {
        const float ry = list_top + static_cast<float>(i - g.first) * row_h;
        const bool  on = (i == picker_.selected());
        if (on) r.draw_round_rect({ix, ry, mw - 40, 30}, RADIUS_SMALL, SURFACE_HI);
        r.draw_text(font, ix + 8, ry + 4, fit_text(font, shown[static_cast<size_t>(i)], mw - 56),
                    on ? TEXT : TEXT_DIM);
    }
    if (shown.empty())
        r.draw_text(font, ix, list_top, "No matches.", TEXT_FAINT);
    if (picker_.filter_open() || !picker_.filter().empty())
        r.draw_text(font, ix, iy + 54,
                    fit_text(font, "Filter: " + std::string(picker_.filter()), mw - 40), TEXT_FAINT);
    if (naming_) {
        r.draw_text(font, ix, my + mh - 92, "New gallery name:", TEXT);
        draw_edit_field(r, font, {ix, my + mh - 60, mw - 40, 40}, name_buf_,
                        name_buf_chrome_, true);
    }
}

void TransferDialog::render_src_galleries_body(gfx::Renderer& r, gfx::FontAtlas& font,
                                               float ix, float iy, float mw, float mh, float my) const
{
    using namespace gfx::theme;
    r.draw_text(font, ix, iy + 36, "Source galleries (Space to select):", TEXT_DIM);
    const float list_top = iy + 72;
    const float row_h    = 34.0f;
    const float list_bottom = my + mh - 20;
    const int   visible_rows = std::max(1, static_cast<int>((list_bottom - list_top) / row_h));
    const auto  g = picker_.geom(visible_rows);
    const auto& shown = picker_.filtered();
    for (int i = g.first; i < g.first + g.visible && i < static_cast<int>(shown.size()); ++i) {
        const float ry = list_top + static_cast<float>(i - g.first) * row_h;
        const bool  on = (i == picker_.selected());
        const bool  checked = picker_.is_checked(shown[static_cast<size_t>(i)]);
        if (on) r.draw_round_rect({ix, ry, mw - 40, 30}, RADIUS_SMALL, SURFACE_HI);
        std::string label = checked ? "[X] " : "[ ] ";
        label += shown[static_cast<size_t>(i)];
        r.draw_text(font, ix + 8, ry + 4, fit_text(font, label, mw - 56), on ? TEXT : TEXT_DIM);
    }
    if (shown.empty())
        r.draw_text(font, ix, list_top, "No matches.", TEXT_FAINT);
    if (picker_.filter_open() || !picker_.filter().empty())
        r.draw_text(font, ix, iy + 54,
                    fit_text(font, "Filter: " + std::string(picker_.filter()), mw - 40), TEXT_FAINT);
}

void TransferDialog::render_body(gfx::Renderer& r, gfx::FontAtlas& font,
                                 float ix, float iy, float mw, float mh, float my)
{
    if (stage_ == Stage::Direction) { render_direction_body(r, font, ix, iy, mw, pull_.direction_sel); return; }
    if (stage_ == Stage::Mode) { render_mode_body(r, font, ix, iy, mw, mode_); return; }
    if (stage_ == Stage::PickingDest) {
        const std::string_view title = pull_.active ? "Source vault:" : "Destination vault:";
        picker_dest_.render(r, font, ix, iy, mw, title);
        return;
    }
    if (stage_ == Stage::Conflict) { render_conflict_body(r, font, ix, iy, mw); return; }
    if (stage_ == Stage::PickSrcGalleries) { render_src_galleries_body(r, font, ix, iy, mw, mh, my); return; }
    render_pick_gallery_body(r, font, ix, iy, mw, mh, my);   // PickGallery
}

} // namespace ui
