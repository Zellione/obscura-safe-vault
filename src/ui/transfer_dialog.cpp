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
#include "platform/vault_registry.h"
#include "ui/keybindings.h"
#include "ui/progress_modal.h"
#include "ui/widgets.h"
#include "vault/transfer.h"

namespace ui {

namespace {
constexpr const char* kNewGalleryRow = "+ New gallery…";
} // namespace

TransferDialog::TransferDialog(vault::Vault& src, std::string src_path,
                               platform::VaultRegistry& registry,
                               platform::FileDialog& dlg, gfx::Window& win)
    : src_(src), src_path_(std::move(src_path)), win_(win), picker_dest_(registry, dlg, win) {}

void TransferDialog::open(std::string src_gallery, std::vector<std::string> filenames)
{
    active_       = true;
    source_       = Source::Images;
    stage_        = Stage::Mode;
    mode_         = vault::TransferMode::Move;
    src_gallery_  = std::move(src_gallery);
    filenames_    = std::move(filenames);
    error_.clear();
    naming_ = false;
    name_buf_.clear();

    // The new-gallery name overlay consumes SDL_EVENT_TEXT_INPUT, which SDL3 only
    // delivers while text input is active.
    SDL_StartTextInput(win_.sdl_window());
}

void TransferDialog::open_gallery(std::string src_gallery)
{
    open("", {});                  // reuse open() to reset all state + build candidates
    source_      = Source::Gallery;
    src_gallery_ = std::move(src_gallery);
}

void TransferDialog::open_galleries(std::vector<std::string> src_paths)
{
    open("", {});                  // reuse open() to reset all state + build candidates
    source_        = Source::Galleries;
    src_galleries_ = std::move(src_paths);
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
    std::vector<std::string> targets = (source_ == Source::Gallery || source_ == Source::Galleries)
        ? vault::gallery_target_parents(dv)
        : vault::image_target_galleries(dv);
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

void TransferDialog::do_move(std::string_view dst_target)
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
                                        mode_, where);
    else if (source_ == Source::Galleries)
        run_.job.start_transfer_galleries(src_, src_galleries_, dv, std::string(dst_target),
                                          mode_, where);
    else
        run_.job.start_transfer_images(src_, src_gallery_, filenames_, dv, std::string(dst_target),
                                       mode_, where);
    stage_ = Stage::Running;
}

// --- per-stage key handlers ------------------------------------------------

bool TransferDialog::handle_mode_key(SDL_Keycode k)
{
    using enum vault::TransferMode;
    if (k == SDLK_UP || k == SDLK_DOWN || k == SDLK_LEFT || k == SDLK_RIGHT)
        mode_ = (mode_ == Move) ? Copy : Move;
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
        picker_dest_.open(src_path_);
        stage_ = Stage::PickingDest;
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

bool TransferDialog::handle_naming_event(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_TEXT_INPUT) { name_buf_ += e.text.text; return true; }
    if (e.type != SDL_EVENT_KEY_DOWN) return true;
    switch (e.key.key) {
        case SDLK_BACKSPACE:
            if (!name_buf_.empty()) name_buf_.pop_back();
            break;
        case SDLK_ESCAPE:
            naming_ = false;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (!name_buf_.empty() &&
                dest_vault().create_gallery(name_buf_) == vault::VaultResult::Ok) {
                rebuild_targets();
                naming_ = false;
                do_move(name_buf_);     // move straight into the new gallery
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

bool TransferDialog::handle_event(const SDL_Event& e)
{
    if (!active_) return false;

    // While the transfer worker runs, swallow all input except Esc → cooperative
    // cancel (files committed so far remain; see transfer_gallery/transfer_images).
    if (stage_ == Stage::Running) {
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) run_.job.cancel();
        return true;
    }

    // PickingDest stage: delegate entirely to picker_dest_ (includes Esc handling).
    if (stage_ == Stage::PickingDest) {
        const bool consumed = picker_dest_.handle_event(e);
        if (!picker_dest_.active()) {
            if (picker_dest_.chosen()) { rebuild_targets(); stage_ = Stage::PickGallery; }
            else                       close();   // Esc inside the picker cancelled the whole dialog
        }
        return consumed;
    }

    // New-gallery name entry (overlays the PickGallery stage).
    if (naming_) return handle_naming_event(e);

    // PickGallery filter typing (Phase 44 Part 1) — '/' opens it; Esc clears the
    // filter before the outer Esc-closes-the-dialog handling below ever sees it.
    if (stage_ == Stage::PickGallery && picker_.filter_open()) {
        if (e.type == SDL_EVENT_TEXT_INPUT) { picker_.filter_append(e.text.text); return true; }
        if (e.type == SDL_EVENT_KEY_DOWN) {
            switch (e.key.key) {
                case SDLK_BACKSPACE: picker_.filter_backspace(); return true;
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
        return true;
    }

    if (e.type != SDL_EVENT_KEY_DOWN) return true;   // modal swallows other events

    const SDL_Keycode k = e.key.key;
    if (k == SDLK_ESCAPE) { close(); return true; }

    using enum Stage;
    switch (stage_) {
        case Mode:        return handle_mode_key(k);
        case PickGallery:
            if (is_search_key(e.key)) { picker_.open_filter(); return true; }
            return handle_gallery_key(k);
        case PickingDest: return true;   // should not reach here (handled above)
        case Running:     return true;   // handled above (Esc→cancel); nothing else
    }
    return true;
}

void TransferDialog::update()
{
    // Drain the background transfer once the worker finishes: record the status,
    // then close() (which wipes the destination key now that it is done with it).
    if (stage_ == Stage::Running) {
        if (auto oc = run_.job.take_outcome()) {
            run_.status = std::move(oc->status);
            run_.done   = true;
            close();
        }
        return;
    }

    if (stage_ == Stage::PickingDest) picker_dest_.update();
}

bool TransferDialog::consume_completed(std::string& status_out)
{
    if (!run_.done) return false;
    status_out = std::move(run_.status);
    run_.done = false;
    return true;
}

// --- render ---------------------------------------------------------------

void TransferDialog::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H) const
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
    const char* verb = (mode_ == vault::TransferMode::Copy) ? "Copy" : "Move";
    r.draw_text(font, ix, iy,
                source_ == Source::Gallery
                    ? std::format("{} gallery \"{}\"", verb,
                                  std::filesystem::path(src_gallery_).filename().string())
                    : source_ == Source::Galleries
                        ? std::format("{} {} galleries", verb, src_galleries_.size())
                        : std::format("{} {} image(s)", verb, filenames_.size()),
                TEXT);

    render_body(r, font, ix, iy, mw, mh, my);

    const std::string& err = !error_.empty() ? error_ : picker_dest_.error();
    if (!err.empty()) r.draw_text(font, ix, my + mh - 30, err, DANGER);
}

void TransferDialog::render_body(gfx::Renderer& r, gfx::FontAtlas& font,
                                 float ix, float iy, float mw, float mh, float my) const
{
    using namespace gfx::theme;

    auto row_list = [&](const std::vector<std::string>& items, int sel, float top) {
        for (size_t i = 0; i < items.size(); ++i) {
            const float ry = top + static_cast<float>(i) * 34.0f;
            const bool on = (static_cast<int>(i) == sel);
            if (on) r.draw_round_rect({ix, ry, mw - 40, 30}, RADIUS_SMALL, SURFACE_HI);
            r.draw_text(font, ix + 8, ry + 4, fit_text(font, items[i], mw - 56),
                        on ? TEXT : TEXT_DIM);
        }
    };

    if (stage_ == Stage::Mode) {
        r.draw_text(font, ix, iy + 36, "Action:", TEXT_DIM);
        const std::vector<std::string> modes = {"Move", "Copy"};
        const int msel = (mode_ == vault::TransferMode::Copy) ? 1 : 0;
        row_list(modes, msel, iy + 72);
        r.draw_text(font, ix, iy + 150, "[Up/Down] choose  [Enter] next", TEXT_FAINT);
    } else if (stage_ == Stage::PickingDest) {
        picker_dest_.render(r, font, ix, iy, mw);
    } else {  // PickGallery
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
                        fit_text(font, "Filter: " + picker_.filter(), mw - 40), TEXT_FAINT);
        if (naming_) {
            r.draw_text(font, ix, my + mh - 92, "New gallery name:", TEXT);
            draw_text_field(r, font, {ix, my + mh - 60, mw - 40, 40}, name_buf_, true);
        }
    }
}

} // namespace ui
