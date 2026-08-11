#include "ui/combine_dialog.h"

#include <format>
#include <utility>
#include <vector>

#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/theme.h"
#include "gfx/window.h"
#include "ui/keybindings.h"
#include "ui/progress_modal.h"
#include "ui/text_input_event.h"
#include "ui/widgets.h"
#include "vault/combine.h"

namespace ui {

CombineDialog::CombineDialog(vault::Vault& src, std::string src_path,
                             platform::VaultRegistry& registry, platform::FileDialog& dlg,
                             gfx::Window& win, SecondVaultSession* second)
    : src_(src), src_path_(std::move(src_path)), win_(win), picker_dest_(registry, dlg, win, second) {}

void CombineDialog::open(std::string src_gallery)
{
    active_      = true;
    stage_       = Stage::Mode;
    mode_        = vault::TransferMode::Move;
    src_gallery_ = std::move(src_gallery);
    error_.clear();
    SDL_StartTextInput(win_.sdl_window());
}

void CombineDialog::close()
{
    // Safe unconditionally: VaultUnlockPicker::close() only locks its transient
    // vault if it's actually unlocked, so this also correctly wipes a
    // chosen-but-not-yet-consumed cross-vault destination if the user backs out
    // (Esc) from PickTarget, not just from PickingDest itself.
    picker_dest_.close();
    SDL_StopTextInput(win_.sdl_window());
    active_ = false;
}

void CombineDialog::choose_target()
{
    const auto& shown = picker_target_.filtered();
    const int sel = picker_target_.selected();
    if (sel < 0 || sel >= static_cast<int>(shown.size())) return;
    do_combine(shown[static_cast<size_t>(sel)]);
}

void CombineDialog::do_combine(const std::string& dst_target)
{
    vault::Vault& dv = picker_dest_.is_self() ? src_ : picker_dest_.unlocked_vault();
    const std::string where = picker_dest_.dest_label();
    run_.job.start_combine(src_, src_gallery_, dv, dst_target, mode_, where);
    stage_ = Stage::Running;
}

// Same Move/Copy toggle convention as TransferDialog::handle_mode_key.
bool CombineDialog::handle_event_mode(const SDL_Event& e)
{
    if (e.type != SDL_EVENT_KEY_DOWN) return true;
    const SDL_Keycode k = e.key.key;
    if (k == SDLK_ESCAPE) { close(); return true; }
    using enum vault::TransferMode;
    if (k == SDLK_UP || k == SDLK_DOWN || k == SDLK_LEFT || k == SDLK_RIGHT)
        mode_ = (mode_ == Move) ? Copy : Move;
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
        picker_dest_.open(src_path_);
        stage_ = Stage::PickingDest;
    }
    return true;
}

void CombineDialog::advance_from_picking_dest()
{
    if (!picker_dest_.chosen()) { close(); return; }
    const vault::Vault& dv = picker_dest_.is_self() ? src_ : picker_dest_.unlocked_vault();
    picker_target_.set_items(vault::combine_target_galleries(dv, src_, src_gallery_));
    stage_ = Stage::PickTarget;
}

bool CombineDialog::handle_event_picking_dest(const SDL_Event& e)
{
    const bool consumed = picker_dest_.handle_event(e);
    if (!picker_dest_.active()) advance_from_picking_dest();
    return consumed;
}

bool CombineDialog::handle_event_target_filter(const SDL_Event& e)
{
    // Precedence rule (Phase 54): the filter field consumes editing keys first.
    // GalleryPickerModel is ONE model with two drivers; this must stay in step
    // with TransferDialog::handle_gallery_filter_event.
    if (const uint64_t rev = picker_target_.filter_input().revision();
        handle_text_input_event(picker_target_.filter_input(), e)) {
        if (picker_target_.filter_input().revision() != rev) picker_target_.refilter();
        return true;
    }
    if (e.type != SDL_EVENT_KEY_DOWN) return true;
    switch (e.key.key) {
        case SDLK_ESCAPE:
            if (!picker_target_.filter().empty()) { picker_target_.filter_clear(); return true; }
            picker_target_.close_filter();
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: choose_target(); return true;
        case SDLK_UP:       picker_target_.move(-1); return true;
        case SDLK_DOWN:     picker_target_.move(1);  return true;
        default: return true;
    }
}

bool CombineDialog::handle_event_pick_target(const SDL_Event& e)
{
    // Same filter-typing convention as TransferDialog's PickGallery.
    if (picker_target_.filter_open()) return handle_event_target_filter(e);

    if (e.type != SDL_EVENT_KEY_DOWN) return true;
    if (e.key.key == SDLK_ESCAPE) { close(); return true; }
    if (is_search_key(e.key)) { picker_target_.open_filter(); return true; }
    if (e.key.key == SDLK_UP)   { picker_target_.move(-1); return true; }
    if (e.key.key == SDLK_DOWN) { picker_target_.move(1);  return true; }
    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) { choose_target(); return true; }
    return true;
}

bool CombineDialog::handle_event(const SDL_Event& e)
{
    if (!active_) return false;

    using enum Stage;
    if (stage_ == Running) {
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) run_.job.cancel();
        return true;
    }
    if (stage_ == Mode) return handle_event_mode(e);
    if (stage_ == PickingDest) return handle_event_picking_dest(e);
    return handle_event_pick_target(e);
}

std::string CombineDialog::selected_target_dest_path() const
{
    if (!(run_.outcome.same_vault && run_.outcome.source_gone)) return {};
    const auto& shown = picker_target_.filtered();
    const int sel = picker_target_.selected();
    if (sel < 0 || sel >= static_cast<int>(shown.size())) return {};
    return shown[static_cast<size_t>(sel)];
}

void CombineDialog::finish_running_stage()
{
    auto oc = run_.job.take_outcome();
    if (!oc) return;
    run_.outcome.status      = oc->ok ? oc->status : oc->error;
    run_.outcome.same_vault  = picker_dest_.is_self();
    // Copy never removes the source — an empty listing there just means the
    // source gallery was empty to begin with, not that it is gone.
    run_.outcome.source_gone =
        mode_ == vault::TransferMode::Move && src_.list(src_gallery_).empty();
    run_.outcome.dest_path   = selected_target_dest_path();
    run_.done = true;
    // Phase 66: a Keep mode hands the unlocked destination to the warm
    // slot (or slides its window) BEFORE close() wipes anything.
    picker_dest_.release_to_slot();
    close();
}

void CombineDialog::update()
{
    if (stage_ == Stage::Running) { finish_running_stage(); return; }
    if (stage_ == Stage::PickingDest) picker_dest_.update();
}

bool CombineDialog::consume_completed(CombineOutcome& out)
{
    if (!run_.done) return false;
    out       = std::move(run_.outcome);
    run_.done = false;
    return true;
}

void CombineDialog::render(gfx::Renderer& r, gfx::FontAtlas& font, float W, float H)
{
    if (!active_) return;

    if (stage_ == Stage::Running) {
        const int total = run_.job.total();
        const int done  = run_.job.done();
        const std::string count =
            total > 0 ? std::format("{} / {} files", done, total) : "Preparing…";
        draw_op_progress(r, font, W, H, {.title = "Combining…", .count_line = count,
                                        .done = done, .total = total});
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
    r.draw_text(font, ix, iy, std::format("Combine \"{}\" into…", src_gallery_), TEXT);

    if (stage_ == Stage::Mode) {
        // Same Move/Copy stage convention as TransferDialog's render_mode_body.
        r.draw_text(font, ix, iy + 36, "Action:", TEXT_DIM);
        const std::vector<std::string> modes = {"Move", "Copy"};
        draw_option_rows(r, font, ix, iy + 72, mw, modes,
                         (mode_ == vault::TransferMode::Copy) ? 1 : 0);
        r.draw_text(font, ix, iy + 150,
                    fit_text(font, "[Up/Down] choose  [Enter] next", mw - 40), TEXT_FAINT);
        return;
    }

    if (stage_ == Stage::PickingDest) {
        picker_dest_.render(r, font, ix, iy, mw);
        if (!picker_dest_.error().empty()) r.draw_text(font, ix, my + mh - 30, picker_dest_.error(), DANGER);
        return;
    }

    // PickTarget
    r.draw_text(font, ix, iy + 36, "Destination gallery:", TEXT_DIM);
    const float list_top    = iy + 72;
    const float row_h       = 34.0f;
    const float list_bottom = my + mh - 20;
    const int   visible_rows = std::max(1, static_cast<int>((list_bottom - list_top) / row_h));
    const auto  g = picker_target_.geom(visible_rows);
    const auto& shown = picker_target_.filtered();
    for (int i = g.first; i < g.first + g.visible && i < static_cast<int>(shown.size()); ++i) {
        const float ry = list_top + static_cast<float>(i - g.first) * row_h;
        const bool  on = (i == picker_target_.selected());
        if (on) r.draw_round_rect({ix, ry, mw - 40, 30}, RADIUS_SMALL, SURFACE_HI);
        r.draw_text(font, ix + 8, ry + 4, fit_text(font, shown[static_cast<size_t>(i)], mw - 56),
                    on ? TEXT : TEXT_DIM);
    }
    if (shown.empty())
        r.draw_text(font, ix, list_top, "No compatible destination galleries.", TEXT_FAINT);
    if (picker_target_.filter_open() || !picker_target_.filter().empty())
        r.draw_text(font, ix, iy + 54,
                    fit_text(font, "Filter: " + std::string(picker_target_.filter()), mw - 40), TEXT_FAINT);
    if (!error_.empty()) r.draw_text(font, ix, my + mh - 30, error_, DANGER);
}

} // namespace ui
